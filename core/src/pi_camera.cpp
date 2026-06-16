#include "vh/pi_camera.hpp"

namespace vh {

const char* to_string(PiCameraError e) noexcept {
  switch (e) {
    case PiCameraError::None: return "None";
    case PiCameraError::NotCompiledIn: return "NotCompiledIn";
    case PiCameraError::InvalidConfig: return "InvalidConfig";
    case PiCameraError::NoCamera: return "NoCamera";
    case PiCameraError::AcquireFailed: return "AcquireFailed";
    case PiCameraError::ConfigureFailed: return "ConfigureFailed";
    case PiCameraError::AllocateFailed: return "AllocateFailed";
    case PiCameraError::StartFailed: return "StartFailed";
    case PiCameraError::NotStarted: return "NotStarted";
    case PiCameraError::CaptureTimeout: return "CaptureTimeout";
    case PiCameraError::CaptureError: return "CaptureError";
  }
  return "Unknown";
}

bool validate_pi_camera_config(const PiCameraConfig& cfg,
                               std::string& error) noexcept {
  if (cfg.capture_width == 0 || cfg.capture_height == 0) {
    error = "capture dims must be positive";
    return false;
  }
  if (cfg.capture_width > kPiCameraMaxDim ||
      cfg.capture_height > kPiCameraMaxDim) {
    error = "capture dims exceed kPiCameraMaxDim";
    return false;
  }
  if (cfg.buffer_count == 0) {
    error = "buffer_count must be >= 1";
    return false;
  }
  if (cfg.frame_timeout_ms <= 0) {
    error = "frame_timeout_ms must be positive";
    return false;
  }
  return true;
}

void PiCameraSource::set_error(PiCameraError e, std::string detail) noexcept {
  last_error_ = e;
  last_error_detail_ = std::move(detail);
}

PiCameraSource::PiCameraSource(PiCameraConfig cfg) : cfg_(cfg) {}

PiCameraSource::~PiCameraSource() { close(); }

bool PiCameraSource::open() {
  std::string err;
  if (!validate_pi_camera_config(cfg_, err)) {
    set_error(PiCameraError::InvalidConfig, std::move(err));
    return false;
  }

#if VH_ENABLE_LIBCAMERA
  return open_libcamera();  // Pi-only path, defined in the libcamera section.
#else
  set_error(PiCameraError::NotCompiledIn,
            "libcamera backend not compiled (VH_ENABLE_LIBCAMERA=OFF)");
  open_ = false;
  return false;
#endif
}

void PiCameraSource::close() {
#if VH_ENABLE_LIBCAMERA
  close_libcamera();
#endif
  open_ = false;
}

std::optional<Frame> PiCameraSource::next_frame() {
  if (!open_) {
    set_error(PiCameraError::NotStarted, "next_frame() before a successful open()");
    return std::nullopt;
  }
#if VH_ENABLE_LIBCAMERA
  return capture_libcamera();
#else
  // Unreachable on desktop: open_ is never set true without libcamera.
  return std::nullopt;
#endif
}

}  // namespace vh

// ===========================================================================
// libcamera backend (Pi builds only, VH_ENABLE_LIBCAMERA=ON).
//
// Trixie ships libcamera 0.3+. API sequence confirmed via NotebookLM
// (2026-06-16): modular headers, formats::R8 for 8-bit grayscale, plane fd via
// SharedFD::get(), row copy honouring StreamConfiguration::stride.
//
// libcamera emits requestCompleted asynchronously on its own thread. next_frame
// is synchronous, so the completion callback pushes finished requests onto a
// queue guarded by a mutex/condition_variable; next_frame waits (bounded by
// frame_timeout_ms) and converts one completed request into a Frame. All of
// this threading is contained here — the pipeline above stays single-threaded.
//
// NOTE: this section is not compiled on desktop and could not be compiled this
// session (no Pi available). It must be built/run on a real Pi (build-test-pi)
// before field use — capture dims, R8 support and stride are verified at
// runtime (fail-closed) but the compile itself is unverified.
// ===========================================================================
#if VH_ENABLE_LIBCAMERA

#include <sys/mman.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

namespace vh {

struct PiCameraSource::Impl {
  std::unique_ptr<libcamera::CameraManager> cm;
  std::shared_ptr<libcamera::Camera> camera;
  std::unique_ptr<libcamera::CameraConfiguration> config;
  libcamera::Stream* stream = nullptr;
  std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
  std::vector<std::unique_ptr<libcamera::Request>> requests;

  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int stride = 0;

  // dmabuf fd -> (mapped base address, mapped length).
  std::unordered_map<int, std::pair<void*, std::size_t>> mappings;

  std::mutex mtx;
  std::condition_variable cv;
  std::queue<libcamera::Request*> completed;
  bool capture_started = false;

  // Runs on a libcamera thread.
  void on_completed(libcamera::Request* req) {
    if (req->status() == libcamera::Request::RequestCancelled) return;
    {
      std::lock_guard<std::mutex> lk(mtx);
      completed.push(req);
    }
    cv.notify_one();
  }
};

namespace {

// Map every unique plane fd once (offset 0, covering offset+length). Returns
// the byte pointer to a plane's pixels, or nullptr on mmap failure.
std::uint8_t* map_plane(PiCameraSource::Impl& impl,
                        const libcamera::FrameBuffer::Plane& plane) {
  const int fd = plane.fd.get();
  auto it = impl.mappings.find(fd);
  if (it == impl.mappings.end()) {
    const std::size_t len = plane.offset + plane.length;
    void* base = ::mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) return nullptr;
    it = impl.mappings.emplace(fd, std::make_pair(base, len)).first;
  }
  return static_cast<std::uint8_t*>(it->second.first) + plane.offset;
}

}  // namespace

bool PiCameraSource::open_libcamera() {
  impl_ = std::make_unique<Impl>();
  Impl& d = *impl_;

  d.cm = std::make_unique<libcamera::CameraManager>();
  if (d.cm->start() != 0) {
    set_error(PiCameraError::AcquireFailed, "CameraManager::start failed");
    close_libcamera();
    return false;
  }

  const auto cams = d.cm->cameras();
  if (cfg_.camera_index >= cams.size()) {
    set_error(PiCameraError::NoCamera, "no camera at requested index");
    close_libcamera();
    return false;
  }
  d.camera = cams[cfg_.camera_index];
  if (d.camera->acquire() != 0) {
    set_error(PiCameraError::AcquireFailed, "Camera::acquire failed");
    close_libcamera();
    return false;
  }

  d.config =
      d.camera->generateConfiguration({libcamera::StreamRole::Viewfinder});
  if (!d.config || d.config->empty()) {
    set_error(PiCameraError::ConfigureFailed, "generateConfiguration failed");
    close_libcamera();
    return false;
  }

  libcamera::StreamConfiguration& sc = d.config->at(0);
  sc.pixelFormat = libcamera::formats::R8;
  sc.size = {cfg_.capture_width, cfg_.capture_height};
  sc.bufferCount = cfg_.buffer_count;

  if (d.config->validate() == libcamera::CameraConfiguration::Invalid) {
    set_error(PiCameraError::ConfigureFailed, "configuration invalid");
    close_libcamera();
    return false;
  }
  // Fail-closed: validate() may have adjusted format/size. We require the exact
  // Gray8 capture geometry the profile asked for — anything else is unsafe.
  if (sc.pixelFormat != libcamera::formats::R8 ||
      sc.size.width != cfg_.capture_width ||
      sc.size.height != cfg_.capture_height) {
    set_error(PiCameraError::ConfigureFailed,
              "R8 / requested capture dims not honoured");
    close_libcamera();
    return false;
  }
  if (d.camera->configure(d.config.get()) != 0) {
    set_error(PiCameraError::ConfigureFailed, "Camera::configure failed");
    close_libcamera();
    return false;
  }

  d.stream = sc.stream();
  d.width = sc.size.width;
  d.height = sc.size.height;
  d.stride = sc.stride;

  d.allocator = std::make_unique<libcamera::FrameBufferAllocator>(d.camera);
  if (d.allocator->allocate(d.stream) < 0) {
    set_error(PiCameraError::AllocateFailed, "FrameBufferAllocator failed");
    close_libcamera();
    return false;
  }

  const auto& buffers = d.allocator->buffers(d.stream);
  for (const auto& buffer : buffers) {
    std::unique_ptr<libcamera::Request> request = d.camera->createRequest();
    if (!request || request->addBuffer(d.stream, buffer.get()) != 0) {
      set_error(PiCameraError::AllocateFailed, "request/addBuffer failed");
      close_libcamera();
      return false;
    }
    d.requests.push_back(std::move(request));
  }

  d.camera->requestCompleted.connect(&d, &Impl::on_completed);
  if (d.camera->start() != 0) {
    set_error(PiCameraError::StartFailed, "Camera::start failed");
    close_libcamera();
    return false;
  }
  d.capture_started = true;
  for (auto& request : d.requests) {
    if (d.camera->queueRequest(request.get()) != 0) {
      set_error(PiCameraError::StartFailed, "queueRequest failed");
      close_libcamera();
      return false;
    }
  }

  open_ = true;
  set_error(PiCameraError::None, "");
  return true;
}

std::optional<Frame> PiCameraSource::capture_libcamera() {
  Impl& d = *impl_;
  libcamera::Request* req = nullptr;
  {
    std::unique_lock<std::mutex> lk(d.mtx);
    const bool got = d.cv.wait_for(
        lk, std::chrono::milliseconds(cfg_.frame_timeout_ms),
        [&d] { return !d.completed.empty(); });
    if (!got) {
      set_error(PiCameraError::CaptureTimeout, "no frame within timeout");
      return std::nullopt;
    }
    req = d.completed.front();
    d.completed.pop();
  }

  std::optional<Frame> result;
  libcamera::FrameBuffer* buffer = req->findBuffer(d.stream);
  if (buffer != nullptr &&
      buffer->metadata().status == libcamera::FrameMetadata::FrameSuccess &&
      !buffer->planes().empty()) {
    const std::uint8_t* src = map_plane(d, buffer->planes()[0]);
    if (src != nullptr) {
      Frame f;
      f.id = frames_captured_;
      f.timestamp_ns = static_cast<std::int64_t>(buffer->metadata().timestamp);
      f.width = d.width;
      f.height = d.height;
      f.format = PixelFormat::Gray8;
      f.payload.resize(static_cast<std::size_t>(d.width) * d.height);
      for (unsigned int y = 0; y < d.height; ++y) {
        std::memcpy(f.payload.data() + static_cast<std::size_t>(y) * d.width,
                    src + static_cast<std::size_t>(y) * d.stride, d.width);
      }
      ++frames_captured_;
      result = std::move(f);
      set_error(PiCameraError::None, "");
    } else {
      set_error(PiCameraError::CaptureError, "mmap failed");
    }
  } else {
    set_error(PiCameraError::CaptureError, "buffer error / not success");
  }

  // Recycle the request regardless of outcome (fail-closed: a bad frame yields
  // nullopt but capture keeps running).
  req->reuse(libcamera::Request::ReuseBuffers);
  d.camera->queueRequest(req);
  return result;
}

void PiCameraSource::close_libcamera() {
  if (!impl_) return;
  Impl& d = *impl_;
  if (d.camera) {
    if (d.capture_started) {
      d.camera->stop();
      d.capture_started = false;
    }
    d.camera->requestCompleted.disconnect();
  }
  for (auto& m : d.mappings) {
    ::munmap(m.second.first, m.second.second);
  }
  d.mappings.clear();
  d.requests.clear();
  if (d.allocator && d.stream) {
    d.allocator->free(d.stream);
  }
  d.allocator.reset();
  {
    std::lock_guard<std::mutex> lk(d.mtx);
    std::queue<libcamera::Request*>().swap(d.completed);
  }
  d.config.reset();
  if (d.camera) {
    d.camera->release();
    d.camera.reset();
  }
  if (d.cm) {
    d.cm->stop();
    d.cm.reset();
  }
  d.stream = nullptr;
  open_ = false;
}

}  // namespace vh

#endif  // VH_ENABLE_LIBCAMERA
