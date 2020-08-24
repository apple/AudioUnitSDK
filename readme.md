# AudioUnitSDK

## Overview
The AudioUnitSDK contains a set of base classes as well as utility sources required for Audio Unit development. These utility classes extend or wrap Core Audio API’s providing developers with the essential scaffolding to create audio effects, instruments, and generators on Apple platforms. They provide an easier to implement C++ class model wrapping the C framework APIs.

## Installing dependencies
1. Install [Xcode][Xcode]

[Xcode]: https://developer.apple.com/xcode/resources/

## Building the project
1. Open AudioUnitSDK.xcodeproj
2. Build the AudioUnitSDK target
3. Add headers from $(BUILT_PRODUCTS_DIR)/usr/local/include/AudioUnitSDK to your projects include path
4. Link libAudioUnitSDK.a to your project


Alternatively, you can add the AudioUnitSDK source directly to your project and build as part of your target. 

## Supported Deployment Targets
macOS (OS X) 10.9 / iOS 9.0 or later.

Copyright (C) 2021 Apple Inc. All rights reserved.