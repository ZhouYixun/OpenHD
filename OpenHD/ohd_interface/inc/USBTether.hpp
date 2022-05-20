//
// Created by consti10 on 19.05.22.
//

#ifndef OPENHD_OPENHD_OHD_INTERFACE_INC_USBHOTSPOT_H_
#define OPENHD_OPENHD_OHD_INTERFACE_INC_USBHOTSPOT_H_

#include "openhd-util-filesystem.hpp"
#include "openhd-util.hpp"
#include <thread>
#include <chrono>
#include <utility>


/**
 * Forward the connect and disconnect event(s) for a USB tethering device.
 * Only supports one USB tethering device connected at the same time.
 */
class USBTetherListener{
 public:
  /**
   * Callback to be called when a new device has been connected/disconnected.
   * @param removed: true if the device has been removed (upper level should stop formwarding)
   * false if the device has been added (upper leved should start forwarding).
   */
  typedef std::function<void(bool removed,std::string ip)> IP_CALLBACK;
  /**
   * Creates a new USB tether listener which notifies the upper level with the IP address of a connected or
   * disconnected USB tether device.
   * @param ip_callback the callback to notify the upper level.
   */
  explicit USBTetherListener(IP_CALLBACK ip_callback):ip_callback(std::move(ip_callback)){}
  /**
   * Continuously checks for connected or disconnected USB tether devices.
   * Does not return as long as there is no fatal error, and blocks the calling thread.
   */
  [[noreturn]] void loopInfinite(){
	while (true){
	  connectOnce();
	}
  }
  /**
   * @return the valid ip address of the connected USB tether device if there is one. Empty if there is currently no device deteced.
   */
  [[nodiscard]] std::vector<std::string> getConnectedTetherIPs()const{
	if(device_ip.empty()){
	  return {};
	}
	return std::vector<std::string>{device_ip};
  }
 private:
  const IP_CALLBACK ip_callback;
  std::string device_ip;
  /**
   * @brief simple state-based method
   * 1) Wait until a tethering device is connected
   * 2) Configure and forward the IP address of the connected device
   * 3) Wait until the device disconnects
   * 4) forward the now disconnected IP address.
   * Nr. 3) might never become true during run time as long as the user does not disconnect his tethering device.
   */
  void connectOnce(){
	const char* connectedDevice="/sys/class/net/usb0";
	// in regular intervals, check if the devices becomes available - if yes, the user connected a ethernet hotspot device.
	while (true){
	  std::this_thread::sleep_for(std::chrono::seconds(1));
	  if(OHDFilesystemUtil::exists(connectedDevice)) {
		std::cout << "Found USB tethering device\n";
		break;
	  }
	}
	// Configure the detected USB tether device (not sure if needed)
	OHDUtil::run_command("dhclient",{"usb0"});

	// now we find the IP of the connected device so we can forward video usw to it
	const auto ip_opt=OHDUtil::run_command_out("ip route show 0.0.0.0/0 dev usb0 | cut -d\\  -f3");
	device_ip=ip_opt.value();

	// check in regular intervals if the tethering device disconnects.
	while (true){
	  std::this_thread::sleep_for(std::chrono::seconds(1));
	  std::cout<<"Checking if USB tethering device disconnected\n";
	  if(!OHDFilesystemUtil::exists(connectedDevice)){
		std::cout<<"USB Tether device disconnected\n";
		break;
	  }
	}
  }
};

// USB hotspot (USB Tethering)
// This was created by translating the tether_functions.sh script form wifibroadcast-scripts into c++.
namespace USBTether{

static void enable(){
  // in regular intervals, check if the devices becomes available - if yes, the user connected a ethernet hotspot device.
  while (true){
	std::this_thread::sleep_for(std::chrono::seconds(1));
	const char* connectedDevice="/sys/class/net/usb0";
	// When the file above becomes available, we almost 100% have a usb device with "mobile hotspot"
	// connected.
	std::cout<<"Checking for USB tethering device\n";
	if(OHDFilesystemUtil::exists(connectedDevice)){
	  std::cout<<"Found USB tethering device\n";
	  // Not sure if this is needed, configure the IP ?!
	  OHDUtil::run_command("dhclient",{"usb0"});

	  // now we find the IP of the connected device so we can forward video usw to it
	  const auto ip_opt=OHDUtil::run_command_out("ip route show 0.0.0.0/0 dev usb0 | cut -d\\  -f3");

	  if(ip_opt!=std::nullopt){
		const std::string ip=ip_opt.value();
		std::cout<<"Found ip:["<<ip<<"]\n";
	  }else{
		std::cerr<<"USBHotspot find ip no success\n";
		return;
	  }
	  // Now that we have hotspot setup and queried the IP,
	  // check in regular intervals if the tethering device is disconnected.
	  while (true){
		std::cout<<"Checking if USB tethering device disconnected\n";
		if(!OHDFilesystemUtil::exists(connectedDevice)){
		  std::cout<<"USB Tether device disconnected\n";
		  return;
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	  }
	}
  }
}

}

#endif //OPENHD_OPENHD_OHD_INTERFACE_INC_USBHOTSPOT_H_
