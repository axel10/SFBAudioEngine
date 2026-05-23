//
// SPDX-FileCopyrightText: 2010 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/SFBAudioEngine
//

#import "SFBFileInputSource.h"

#import "SFBInputSource+Internal.h"

#import <stdio.h>
#import <sys/stat.h>
#import <fcntl.h>

@interface SFBFileInputSource () {
  @private
    struct stat _filestats;
    FILE *_file;
    void *_buffer;
}
@end

@implementation SFBFileInputSource

- (BOOL)openReturningError:(NSError **)error {
    _file = fopen(_url.fileSystemRepresentation, "r");
    if (!_file) {
        int err = errno;
        os_log_error(gSFBInputSourceLog, "fopen failed: %{public}s (%d)", strerror(err), err);
        if (error) {
            *error = [self posixErrorWithCode:err];
        }
        return NO;
    }

    if (fstat(fileno(_file), &_filestats) == -1) {
        int err = errno;
        os_log_error(gSFBInputSourceLog, "fstat failed: %{public}s (%d)", strerror(err), err);
        if (error) {
            *error = [self posixErrorWithCode:err];
        }

        if (fclose(_file)) {
            os_log_info(gSFBInputSourceLog, "fclose failed: %{public}s (%d)", strerror(errno), errno);
        }
        _file = NULL;

        return NO;
    }

    _buffer = malloc(256 * 1024);
    if (_buffer) {
        if (setvbuf(_file, (char *)_buffer, _IOFBF, 256 * 1024) != 0) {
            os_log_debug(gSFBInputSourceLog, "setvbuf failed");
        }
    } else {
        os_log_debug(gSFBInputSourceLog, "malloc for setvbuf failed");
    }

    int fd = fileno(_file);
    struct radvisory advice = { .ra_offset = 0, .ra_count = 256 * 1024 };
    if (fcntl(fd, F_RDADVISE, &advice) == -1) {
        os_log_debug(gSFBInputSourceLog, "fcntl F_RDADVISE failed");
    }

    return YES;
}

- (BOOL)closeReturningError:(NSError **)error {
    if (_file) {
        int result = fclose(_file);
        _file = NULL;
        if (_buffer) {
            free(_buffer);
            _buffer = NULL;
        }
        if (result) {
            int err = errno;
            os_log_error(gSFBInputSourceLog, "fclose failed: %{public}s (%d)", strerror(err), err);
            if (error) {
                *error = [self posixErrorWithCode:err];
            }
            return NO;
        }
    }
    return YES;
}

- (BOOL)isOpen {
    return _file != NULL;
}

- (BOOL)readBytes:(void *)buffer length:(NSInteger)length bytesRead:(NSInteger *)bytesRead error:(NSError **)error {
    NSParameterAssert(buffer != NULL);
    NSParameterAssert(length >= 0);
    NSParameterAssert(bytesRead != NULL);

    size_t read = fread(buffer, 1, (size_t)length, _file);
    if (read != (size_t)length && ferror(_file)) {
        int err = errno;
        os_log_error(gSFBInputSourceLog, "fread error: %{public}s (%d)", strerror(err), err);
        if (error) {
            *error = [self posixErrorWithCode:err];
        }
        return NO;
    }
    *bytesRead = (NSInteger)read;
    return YES;
}

- (BOOL)atEOF {
    return feof(_file) != 0;
}

- (BOOL)getOffset:(NSInteger *)offset error:(NSError **)error {
    NSParameterAssert(offset != NULL);
    off_t result = ftello(_file);
    if (result == -1) {
        int err = errno;
        os_log_error(gSFBInputSourceLog, "ftello failed: %{public}s (%d)", strerror(err), err);
        if (error) {
            *error = [self posixErrorWithCode:err];
        }
        return NO;
    }
    *offset = result;
    return YES;
}

- (BOOL)getLength:(NSInteger *)length error:(NSError **)error {
    NSParameterAssert(length != NULL);
    *length = _filestats.st_size;
    return YES;
}

- (BOOL)supportsSeeking {
    // Regular files are always seekable.
    // Punt on testing whether ftello() and fseeko() actually work.
    return S_ISREG(_filestats.st_mode);
}

- (BOOL)seekToOffset:(NSInteger)offset error:(NSError **)error {
    NSParameterAssert(offset >= 0);
    if (fseeko(_file, offset, SEEK_SET)) {
        int err = errno;
        os_log_error(gSFBInputSourceLog, "fseeko(%ld, SEEK_SET) error: %{public}s (%d)", (long)offset, strerror(err),
                     err);
        if (error) {
            *error = [self posixErrorWithCode:err];
        }
        return NO;
    }
    return YES;
}

@end
