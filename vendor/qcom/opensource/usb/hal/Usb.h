#ifndef ANDROID_HARDWARE_USB_V1_2_USB_H
#define ANDROID_HARDWARE_USB_V1_2_USB_H

#include <android/hardware/usb/1.2/IUsb.h>
#include <android/hardware/usb/1.2/types.h>
#include <android/hardware/usb/1.2/IUsbCallback.h>
#include <hidl/Status.h>
#include <utils/Log.h>
#include <android-base/properties.h>

#define UEVENT_MSG_LEN 2048
// The type-c stack waits for 4.5 - 5.5 secs before declaring a port non-pd.
// The -partner directory would not be created until this is done.
// Having a margin of ~3 secs for the directory and other related bookeeping
// structures created and uvent fired.
#define PORT_TYPE_TIMEOUT 8

namespace android {
namespace hardware {
namespace usb {
namespace V1_2 {
namespace implementation {

using ::android::hardware::usb::V1_0::PortRole;
using ::android::hardware::usb::V1_0::PortRoleType;
using ::android::hardware::usb::V1_0::PortDataRole;
using ::android::hardware::usb::V1_0::PortPowerRole;
using ::android::hardware::usb::V1_0::Status;
using ::android::hardware::usb::V1_1::PortMode_1_1;
using ::android::hardware::usb::V1_1::PortStatus_1_1;
using ::android::hardware::usb::V1_2::ContaminantDetectionStatus;
using ::android::hardware::usb::V1_2::ContaminantProtectionMode;
using ::android::hardware::usb::V1_2::ContaminantProtectionStatus;
using ::android::hardware::usb::V1_2::PortStatus;
using ::android::hardware::usb::V1_2::IUsb;
using ::android::hardware::usb::V1_2::IUsbCallback;
using ::android::hidl::base::V1_0::DebugInfo;
using ::android::hidl::base::V1_0::IBase;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::base::SetProperty;
using ::android::base::GetProperty;
using ::android::sp;

struct Usb : public IUsb {
    Usb();

    Return<void> switchRole(const hidl_string& portName, const V1_0::PortRole& role) override;
    Return<void> setCallback(const sp<V1_0::IUsbCallback>& callback) override;
    Return<void> queryPortStatus() override;
    Return<void> enableContaminantPresenceProtection(const hidl_string &portName, bool enable) override;
    Return<void> enableContaminantPresenceDetection(const hidl_string &portName, bool enable) override;

    sp<V1_0::IUsbCallback> mCallback_1_0;
    // Protects mCallback variable
    pthread_mutex_t mLock;
    // Protects roleSwitch operation
    pthread_mutex_t mRoleSwitchLock;
    // Threads waiting for the partner to come back wait here
    pthread_cond_t mPartnerCV;
    // lock protecting mPartnerCV
    pthread_mutex_t mPartnerLock;
    // Variable to signal partner coming back online after type switch
    bool mPartnerUp;
    // Variable to indicate presence or absence or contaminant
    bool mContaminantPresence;
    // Variable to indicate presence or absence of wakeup node
    bool mIgnoreWakeup;
    // Configuration descriptor for MaxPower
    std::string mMaxPower;
    // Configuration descriptor for bmAttributes
    std::string mAttributes;
    // Current power operation mode
    std::string mPowerOpMode;
    // Path to get the status of contaminant presence
    std::string mContaminantStatusPath;

    private:
        pthread_t mPoll;
};

}  // namespace implementation
}  // namespace V1_2
}  // namespace usb
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_USB_V1_2_USB_H
