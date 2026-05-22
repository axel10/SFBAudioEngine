//
// SPDX-FileCopyrightText: 2012 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/SFBAudioEngine
//

import XCTest
@testable import SFBAudioEngine

final class SFBAudioEngineTests: XCTestCase {
    func testInputSourceFromData() throws {
        let input = InputSource(data: Data(repeating: 0xfe, count: 16))
        XCTAssertEqual(input.isOpen, true)
        XCTAssertEqual(input.supportsSeeking, true)
        XCTAssertEqual(try input.offset, 0)
        let i: UInt8 = try input.read()
        XCTAssertEqual(i, 0xfe)
        XCTAssertEqual(try input.offset, 1)
        XCTAssertEqual(try input.length, 16)
    }

    func testOutputTargetFromData() throws {
        let output = OutputTarget.makeForData()
        XCTAssertEqual(output.isOpen, true)
        XCTAssertEqual(output.supportsSeeking, true)
        var i: UInt32 = 0x12345678
        XCTAssertEqual(try output.write(&i, length: MemoryLayout<UInt32>.size), MemoryLayout<UInt32>.size)
        try output.seek(toOffset: 0)
        XCTAssertEqual(try output.read(&i, length: MemoryLayout<UInt32>.size), MemoryLayout<UInt32>.size)
        XCTAssertEqual(i, 0x12345678)
    }

    func testACDC() throws {
        let url = URL(fileURLWithPath: "/Volumes/Untitled/music/AcDc - Shoot To Thrill.flac")
        guard FileManager.default.fileExists(atPath: url.path) else {
            print("ACDC file does not exist, skipping test")
            return
        }
        let decoder = try AudioDecoder(url: url)
        try decoder.open()
        XCTAssertEqual(decoder.isOpen, true)
        print("--- ACDC TEST START ---")
        print("Decoder format: \(decoder.processingFormat)")
        print("Total frames: \(decoder.length)")
        
        let buffer = AVAudioPCMBuffer(pcmFormat: decoder.processingFormat, frameCapacity: 4096)!
        var loops = 0
        while loops < 1000000 {
            loops += 1
            do {
                try decoder.decode(into: buffer, length: 4096)
            } catch {
                print("decoder.decode threw error on loop \(loops): \(error)")
                break
            }
            if buffer.frameLength == 0 {
                print("Reached end of file normally on loop \(loops). Current position: \(decoder.position)")
                break
            }
            // Reset buffer frameLength for next read
            buffer.frameLength = 0
        }
        print("Loops run: \(loops), final position: \(decoder.position)")
        print("--- ACDC TEST END ---")
        try decoder.close()
    }
}


