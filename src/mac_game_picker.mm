#include "mac_game_picker.h"
#include "game_check.h"
#import <AppKit/AppKit.h>

#include <memory>

namespace {

NSString* const kGamePathKey = @"GamePath";

using Ready = std::shared_ptr<std::function<void(std::filesystem::path)>>;

void Ask(Ready ready, NSString* problem);

// The game window, so the panels come up as its sheets (also in fullscreen).
NSWindow* GameWindow() {
  return NSApp.keyWindow ?: NSApp.mainWindow ?: NSApp.windows.firstObject;
}

void OfferRetry(Ready ready) {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Unreal Tournament 3 needs your game";
  alert.informativeText = @"No game content comes with the app. Choose a disc image (ISO/XISO) "
                          @"of your Xbox 360 disc, or the folder of the extracted disc.";
  [alert addButtonWithTitle:@"Choose Game…"];
  [alert addButtonWithTitle:@"Quit"];
  void (^done)(NSModalResponse) = ^(NSModalResponse response) {
    if (response == NSAlertFirstButtonReturn) {
      dispatch_async(dispatch_get_main_queue(), ^{ Ask(ready, nil); });
    } else {
      [NSApp terminate:nil];
    }
  };
  if (NSWindow* window = GameWindow()) {
    [alert beginSheetModalForWindow:window completionHandler:done];
  } else {
    done([alert runModal]);
  }
}

void Ask(Ready ready, NSString* problem) {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseFiles = YES;
  panel.canChooseDirectories = YES;
  panel.allowsMultipleSelection = NO;
  panel.prompt = @"Play";
  NSString* message = @"Choose your Unreal Tournament 3 Xbox 360 disc image (ISO/XISO), or the "
                      @"folder of the extracted disc (the one holding default.xex).";
  panel.message = problem.length ? [NSString stringWithFormat:@"%@\n%@", problem, message] : message;
  void (^done)(NSModalResponse) = ^(NSModalResponse response) {
    NSURL* url = panel.URL;
    if (response != NSModalResponseOK || !url) {
      dispatch_async(dispatch_get_main_queue(), ^{ OfferRetry(ready); });
      return;
    }
    // Checked off the main thread: an image on a slow drive takes a moment.
    const std::filesystem::path path(url.path.fileSystemRepresentation);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      const std::string check = ut3::CheckGame(ut3::GameSource(path));
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!check.empty()) {
          Ask(ready, [NSString stringWithUTF8String:check.c_str()]);
          return;
        }
        [NSUserDefaults.standardUserDefaults setObject:url.path forKey:kGamePathKey];
        (*ready)(path);
      });
    });
  };
  if (NSWindow* window = GameWindow()) {
    [panel beginSheetModalForWindow:window completionHandler:done];
  } else {
    [panel beginWithCompletionHandler:done];
  }
}

}  // namespace

std::filesystem::path Ut3SavedGamePath() {
  NSString* path = [NSUserDefaults.standardUserDefaults stringForKey:kGamePathKey];
  return path.length ? std::filesystem::path(path.fileSystemRepresentation)
                     : std::filesystem::path();
}

void Ut3ChooseGame(std::function<void(std::filesystem::path)> ready, std::string problem) {
  Ask(std::make_shared<std::function<void(std::filesystem::path)>>(std::move(ready)),
      problem.empty() ? nil : [NSString stringWithUTF8String:problem.c_str()]);
}
