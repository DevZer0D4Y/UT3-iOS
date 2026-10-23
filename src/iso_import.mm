#include "iso_import.h"
#include "game_check.h"
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface Ut3ISOImporter : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, strong) UIViewController* presenter;
@property(nonatomic, copy) NSString* problem;
- (void)offerImport;
@end

@implementation Ut3ISOImporter {
  std::function<void(std::filesystem::path)> ready_;
}
- (instancetype)initWithReady:(std::function<void(std::filesystem::path)>)ready {
  if ((self = [super init])) ready_ = std::move(ready);
  return self;
}
- (void)offerImport {
  NSString* message = @"Select your Xbox 360 ISO/XISO, or copy extracted game files into the game folder in Files. Saves are kept separately.";
  if (self.problem.length) {
    message = [NSString stringWithFormat:@"%@\n\n%@", self.problem, message];
    self.problem = nil;
  }
  UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Load Unreal Tournament 3"
      message:message
      preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"Choose ISO" style:UIAlertActionStyleDefault handler:^(UIAlertAction* action) {
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeData] asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self.presenter presentViewController:picker animated:YES completion:nil];
  }]];
  [self.presenter presentViewController:alert animated:YES completion:nil];
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
  [controller dismissViewControllerAnimated:YES completion:^{ [self offerImport]; }];
}
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  NSURL* source = urls.firstObject;
  if (!source) { [self offerImport]; return; }
  BOOL scoped = [source startAccessingSecurityScopedResource];
  UIAlertController* progress = [UIAlertController alertControllerWithTitle:@"Importing ISO"
      message:@"Checking the disc image, then copying it. Keep the app open: large disc images can take several minutes."
      preferredStyle:UIAlertControllerStyleAlert];
  [controller dismissViewControllerAnimated:YES completion:^{
    [self.presenter presentViewController:progress animated:YES completion:nil];
  }];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    NSURL* docs = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
    NSURL* stage = [docs URLByAppendingPathComponent:@"game.importing"];
    NSURL* target = [docs URLByAppendingPathComponent:@"game.iso"];
    NSFileManager* fm = NSFileManager.defaultManager;
    [fm removeItemAtURL:stage error:nil];
    NSError* error = nil;
    __block BOOL copied = NO;
    NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    __block NSError* copyError = nil;
    [coordinator coordinateReadingItemAtURL:source options:0 error:&error byAccessor:^(NSURL* readable) {
      // Checked where it is, before copying gigabytes of an image that cannot
      // be used.
      const std::string problem =
          ut3::CheckGame(ut3::GameSource(std::filesystem::path(readable.path.UTF8String)));
      if (!problem.empty()) {
        copyError = [NSError errorWithDomain:@"Ut3ISO" code:1 userInfo:@{
          NSLocalizedDescriptionKey: [NSString stringWithUTF8String:problem.c_str()]}];
        return;
      }
      copied = [fm copyItemAtURL:readable toURL:stage error:&copyError];
    }];
    if (!error) error = copyError;
    if (copied && !error) {
      if ([fm fileExistsAtPath:target.path]) {
        copied = [fm replaceItemAtURL:target withItemAtURL:stage backupItemName:nil options:0 resultingItemURL:nil error:&error];
      } else {
        copied = [fm moveItemAtURL:stage toURL:target error:&error];
      }
    }
    if (scoped) [source stopAccessingSecurityScopedResource];
    if (!copied) [fm removeItemAtURL:stage error:nil];
    dispatch_async(dispatch_get_main_queue(), ^{
      [progress dismissViewControllerAnimated:YES completion:^{
        if (copied && !error) {
          ready_(std::filesystem::path(target.path.UTF8String));
        } else {
          UIAlertController* failure = [UIAlertController alertControllerWithTitle:@"Could not import ISO"
              message:error.localizedDescription ?: @"Check available storage and try again."
              preferredStyle:UIAlertControllerStyleAlert];
          [failure addAction:[UIAlertAction actionWithTitle:@"Try again" style:UIAlertActionStyleDefault handler:^(UIAlertAction* action) { [self offerImport]; }]];
          [self.presenter presentViewController:failure animated:YES completion:nil];
        }
      }];
    });
  });
}
@end

void Ut3ImportISO(std::function<void(std::filesystem::path)> ready, std::string problem) {
  // Retained while a provider asynchronously supplies the selected document.
  static Ut3ISOImporter* importer;
  importer = [[Ut3ISOImporter alloc] initWithReady:std::move(ready)];
  if (!problem.empty()) importer.problem = [NSString stringWithUTF8String:problem.c_str()];
  for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:UIWindowScene.class]) continue;
    for (UIWindow* window in ((UIWindowScene*)scene).windows) {
      if (window.isKeyWindow) { importer.presenter = window.rootViewController; break; }
    }
    if (importer.presenter) break;
  }
  [importer offerImport];
}
