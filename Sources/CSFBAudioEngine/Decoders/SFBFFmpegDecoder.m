//
// SPDX-FileCopyrightText: 2013 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/SFBAudioEngine
//

// Keep the FFmpeg decoder implementation in one place under Extra/, but compile
// it as part of the default SwiftPM target on macOS.
#import <TargetConditionals.h>

#if TARGET_OS_OSX || TARGET_OS_IOS
#import "../../../Extra/SFBFFmpegDecoder.m"
#endif
