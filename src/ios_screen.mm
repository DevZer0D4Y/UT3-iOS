// Keep the screen on while the game runs. iOS dims and locks after the
// Auto-Lock delay without touches, and a Bluetooth controller's input does not
// count as one: matches played on a controller locked the phone (and suspended
// the game) after two minutes.
#include "ios_screen.h"
#import <UIKit/UIKit.h>

void Ut3KeepScreenAwake(bool awake) {
  dispatch_async(dispatch_get_main_queue(), ^{
    UIApplication.sharedApplication.idleTimerDisabled = awake;
  });
}
