# AudioUnitSDK

## Overview
The AudioUnitSDK contains a set of base classes as well as utility sources required for Audio Unit development. These utility classes extend or wrap Core Audio API’s providing developers with the essential scaffolding to create audio effects, instruments, and generators on Apple platforms. They provide an easier to implement C++ class model wrapping the C framework APIs.

## Installing dependencies
1. Install [Xcode][Xcode]

[Xcode]: https://developer.apple.com/xcode/resources/

## Building the project
1. Open AudioUnitSDK.xcodeproj
2. Build the AudioUnitSDK target
3. Add the `include` folder to your projects Header Search Paths
4. Link libAudioUnitSDK.a to your project


Alternatively, you can add the AudioUnitSDK source directly to your project and build as part of your target. 

## Supported Deployment Targets
macOS (OS X) 10.9 / iOS 9.0 or later.

## Changelog

### Version 1.3.0

- Minor updates and bug fixes.

### Version 1.2.0

##### Additions

- New header `AUConfig.h` for improved project organization.

##### Changes

- C++ language version to `C++20` for modern language features.

### Version 1.1.0

##### Changes

- The `Source` folder was split in two folders: `include` for public headers, and `src` for private source files.
Users building the AudioUnitSDK sources from within their Xcode project should update the source file locations and change the include path to `path/to/AudioUnitSDK/include`. 
Include directives should be prefixed with AudioUnitSDK (i.e. `#include "AudioUnitSDK/AUBase.h"` instead of `#include "AUBase.h"`).

### Version 1.0.0

- Initial upload.

Copyright (C) 2023 Apple Inc. All rights reserved.
