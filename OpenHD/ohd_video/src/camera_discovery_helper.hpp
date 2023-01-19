//
// Created by consti10 on 16.05.22.
//

#ifndef OPENHD_DCAMERASHELPER_H
#define OPENHD_DCAMERASHELPER_H

#include "openhd-util.hpp"
#include <libusb.h>
#include <string>
#include <vector>

#include "camera_enums.hpp"

/**
 * Helper for the discover thermal cameras step.
 * It is a bit more complicated, once we actually support them the code here
 * will probably blow a bit. Rn I just copy pasted stephens code for the flir
 * and seek here
 */
namespace DThermalCamerasHelper {
static constexpr auto FLIR_ONE_VENDOR_ID = 0x09cb;
static constexpr auto FLIR_ONE_PRODUCT_ID = 0x1996;

static constexpr auto SEEK_COMPACT_VENDOR_ID = 0x289d;
static constexpr auto SEEK_COMPACT_PRODUCT_ID = 0x0010;

static constexpr auto SEEK_COMPACT_PRO_VENDOR_ID = 0x289d;
static constexpr auto SEEK_COMPACT_PRO_PRODUCT_ID = 0x0011;
/*
 * What this is:
 *
 * We're detecting whether the flir one USB thermal camera is connected. We then
 * run the flir one driver with systemd.
 *
 * What happens after:
 *
 * The systemd service starts, finds the camera and begins running on the device
 * node we select. Then we will let it be found by the rest of this class just
 * like any other camera, so it gets recorded in the manifest and found by the
 * camera service.
 *
 *
 * todo: this should really be marking the camera as a thermal cam instead of
 * starting v4l2loopback and abstracting it away like this, but the camera
 * service doesn't yet have a thermal handling class
 */
static void enableFlirIfFound() {
  libusb_context *context = nullptr;
  int result = libusb_init(&context);
  if (result) {
    openhd::log::get_default()->warn("Failed to initialize libusb");
    return;
  }
  libusb_device_handle *handle = libusb_open_device_with_vid_pid(
      nullptr, FLIR_ONE_VENDOR_ID, FLIR_ONE_PRODUCT_ID);
  if(!handle)return;
  OHDUtil::run_command("systemctl", {"start", "flirone"});
  libusb_close(handle);
}

/*
 * What this is:
 *
 * We're detecting whether the 2 known Seek thermal USB cameras are connected,
 * then constructing arguments for the seekthermal driver depending on which
 * model it is. We then run the seek driver with systemd using the arguments
 * file we provided to it in seekthermal.service in the libseek-thermal package.
 *
 * What happens after:
 *
 * The systemd service starts, finds the camera and begins running on the device
 * node we select. Then we will let it be found by the rest of this class just
 * like any other camera, so it gets recorded in the manifest and found by the
 * camera service.
 *
 *
 * todo: this should pull the camera settings from the settings file if
 * available
 */
static void enableSeekIfFound() {
  libusb_context *context = nullptr;
  int result = libusb_init(&context);
  if (result) {
   openhd::log::get_default()->warn("Failed to initialize libusb");
    return;
  }

  libusb_device_handle *handle_compact = libusb_open_device_with_vid_pid(
      nullptr, SEEK_COMPACT_VENDOR_ID, SEEK_COMPACT_PRODUCT_ID);
  libusb_device_handle *handle_compact_pro = libusb_open_device_with_vid_pid(
      nullptr, SEEK_COMPACT_PRO_VENDOR_ID, SEEK_COMPACT_PRO_PRODUCT_ID);

  // todo: this will need to be pulled from the config, we may end up running
  // these from the camera service so that
  //       it can see the camera settings, which are not visible to
  //       openhd-system early at boot
  std::string model;
  std::string fps;

  if (handle_compact) {
    openhd::log::get_default()->debug("Found seek");
    model = "seek";
    fps = "7";
  }

  if (handle_compact_pro) {
    openhd::log::get_default()->debug("Found seekpro");
    model = "seekpro";
    // todo: this is not necessarily accurate, not all compact pro models are
    // 15hz
    fps = "15";
  }

  if (handle_compact_pro || handle_compact) {
    openhd::log::get_default()->debug("Found seek thermal camera");

    std::ofstream _u("/etc/openhd/seekthermal.conf",
                     std::ios::binary | std::ios::out);
    // todo: this should be more dynamic and allow for multiple cameras
    _u << "DeviceNode=/dev/video4";
    _u << std::endl;
    _u << "SeekModel=";
    _u << model;
    _u << std::endl;
    _u << "FPS=";
    _u << fps;
    _u << std::endl;
    _u << "SeekColormap=11";
    _u << std::endl;
    _u << "SeekRotate=11";
    _u << std::endl;
    _u.close();

    std::vector<std::string> ar{"start", "seekthermal"};
    OHDUtil::run_command("systemctl", ar);
  }
}
}  // namespace DThermalCamerasHelper


/**
 * Try and break out some of the stuff from stephen.
 * Even though it mght not be re-used in multiple places, it makes the code more
 * readable in my opinion.
 */
namespace openhd::v4l2 {
/**
 * Search for all v4l2 video devices, that means devices named /dev/videoX where
 * X=0,1,...
 * @return list of all the devices that have the above name scheme.
 */
static std::vector<std::string> findV4l2VideoDevices() {
  const auto paths =
      OHDFilesystemUtil::getAllEntriesFullPathInDirectory("/dev");
  std::vector<std::string> ret;
  const std::regex r{"/dev/video([\\d]+)"};
  for (const auto &path : paths) {
    std::smatch result;
    if (!std::regex_search(path, result, r)) {
      continue;
    }
    ret.push_back(path);
  }
  return ret;
}

// Util so we can't forget to close the fd
class V4l2FPHolder{
 public:
  V4l2FPHolder(const std::string &node,const PlatformType& platform_type){
    // fucking hell, on jetson v4l2_open seems to be bugged
    // https://forums.developer.nvidia.com/t/v4l2-open-create-core-with-jetpack-4-5-or-later/170624/6
    if(platform_type==PlatformType::Jetson){
      fd = open(node.c_str(), O_RDWR | O_NONBLOCK, 0);
    }else{
      fd = v4l2_open(node.c_str(), O_RDWR);
    }
  }
  ~V4l2FPHolder(){
    if(fd!=-1){
      v4l2_close(fd);
    }
  }
  [[nodiscard]] bool opened_successfully() const{
    return fd!=-1;
  }
  int fd;
};

// Stephen already wrote the parsing for this info, even though it is not really needed
// I keep it anyways
struct Udevaddm_info {
  std::string id_model="unknown";
  std::string id_vendor="unknown";
};
Udevaddm_info get_udev_adm_info(const std::string& v4l2_device,std::shared_ptr<spdlog::logger>& m_console){
  Udevaddm_info ret{};
  const auto udev_info_opt=OHDUtil::run_command_out(fmt::format("udevadm info {}",v4l2_device));
  if(udev_info_opt==std::nullopt){
    m_console->debug("udev_info no result");
    return {};
  }
  const auto& udev_info=udev_info_opt.value();
  // check for device name
  std::smatch model_result;
  const std::regex model_regex{"ID_MODEL=([\\w]+)"};
  if (std::regex_search(udev_info, model_result, model_regex)) {
    if (model_result.size() == 2) {
      ret.id_model = model_result[1];
    }
  }
  // check for device vendor
  std::smatch vendor_result;
  const std::regex vendor_regex{"ID_VENDOR=([\\w]+)"};
  if (std::regex_search(udev_info, vendor_result, vendor_regex)) {
    if (vendor_result.size() == 2) {
      ret.id_vendor = vendor_result[1];
    }
  }
  return ret;
}

static std::string v4l2_capability_to_string(const v4l2_capability caps){
  return fmt::format("driver:{},bus_info:{}",caps.driver,caps.bus_info);
}

static std::optional<v4l2_capability> get_capabilities(std::unique_ptr<openhd::v4l2::V4l2FPHolder>& v4l2_fp_holder){
  struct v4l2_capability caps = {};
  if (ioctl(v4l2_fp_holder->fd, VIDIOC_QUERYCAP, &caps) == -1) {
    return std::nullopt;
  }
  return caps;
}

struct EndpointFormats{
  // These are the 3 (already encoded) formats openhd understands
  std::vector<EndpointFormat> formats_h264;
  std::vector<EndpointFormat> formats_h265;
  std::vector<EndpointFormat> formats_mjpeg;
  // anything other (raw) we pack into a generic bucket
  std::vector<EndpointFormat> formats_raw;
  bool has_any_valid_format=false;
};
// Enumerate all the ("pixel formats") we are after for a given v4l2 device
static EndpointFormats iterate_supported_outputs(std::unique_ptr<openhd::v4l2::V4l2FPHolder>& v4l2_fp_holder){
  auto m_console=openhd::log::get_default();
  EndpointFormats ret{};

  struct v4l2_fmtdesc fmtdesc{};
  memset(&fmtdesc, 0, sizeof(fmtdesc));
  fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  while (ioctl(v4l2_fp_holder->fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
    struct v4l2_frmsizeenum frmsize{};
    frmsize.pixel_format = fmtdesc.pixelformat;
    frmsize.index = 0;
    while (ioctl(v4l2_fp_holder->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
      struct v4l2_frmivalenum frmival{};
      if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        frmival.index = 0;
        frmival.pixel_format = fmtdesc.pixelformat;
        frmival.width = frmsize.discrete.width;
        frmival.height = frmsize.discrete.height;
        while (ioctl(v4l2_fp_holder->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
          if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            EndpointFormat endpoint_format;
            endpoint_format.format = fmt::format("{}", fmtdesc.description);
            endpoint_format.width = frmsize.discrete.width;
            endpoint_format.height = frmsize.discrete.height;
            endpoint_format.fps = frmival.discrete.denominator;
            //m_console->debug("{}", endpoint_format.debug());
            ret.has_any_valid_format= true;
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264) {
              ret.formats_h264.push_back(endpoint_format);
            }
#if defined V4L2_PIX_FMT_H265
            else if (fmtdesc.pixelformat == V4L2_PIX_FMT_H265) {
              ret.formats_h265.push_back(endpoint_format);
            }
#endif
            else if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
              ret.formats_mjpeg.push_back(endpoint_format);
            } else {
              // if it supports something else we assume it's one of the raw formats, being specific here is too complicated
              ret.formats_raw.push_back(endpoint_format);
            }
          }
          frmival.index++;
        }
      }
      frmsize.index++;
    }
    fmtdesc.index++;
  }
  return ret;
}

/**
 * Helper for checking if a v4l2 device can output any of the supported endpoint format(s).
 * Returns std::nullopt if this device cannot do h264,h265,mjpeg or RAW out.
 */
struct XValidEndpoint{
  v4l2_capability caps;
  openhd::v4l2::EndpointFormats formats;
};
static std::optional<XValidEndpoint> probe_v4l2_device(const PlatformType platform_tpye,std::shared_ptr<spdlog::logger>& m_console,const std::string& device_node){
  auto v4l2_fp_holder=std::make_unique<openhd::v4l2::V4l2FPHolder>(device_node,platform_tpye);
  if(!v4l2_fp_holder->opened_successfully()){
    m_console->debug("Can't open {}",device_node);
    return std::nullopt;
  }
  const auto caps_opt=openhd::v4l2::get_capabilities(v4l2_fp_holder);
  if(!caps_opt){
    m_console->debug("Can't get caps for {}",device_node);
    return std::nullopt;
  }
  const auto caps=caps_opt.value();
  const auto supported_formats=openhd::v4l2::iterate_supported_outputs(v4l2_fp_holder);
  if(supported_formats.has_any_valid_format){
    return XValidEndpoint{caps,supported_formats};
  }
  return std::nullopt;
}

}  // namespace openhd::v4l2

#endif  // OPENHD_DCAMERASHELPER_H
