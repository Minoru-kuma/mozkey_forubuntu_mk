// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_BASE_MOZKEY_PROFILE_H_
#define MOZC_BASE_MOZKEY_PROFILE_H_
#include <string_view>
namespace mozc {
// Linux Mozkey only. Blocking filesystem work: never call on the Fcitx loop.
// Reject symlinks in every component; only an owned private final directory
// is accepted. Creation, if requested, is restricted to the final component.
// Caller owns the CLOEXEC descriptor. Errors return -1 with errno set.
int OpenMozkeyProfileDirectory(std::string_view path, bool create_leaf = false);
// Open a private regular 0600, single-link lock/key file without truncating,
// chmod'ing or locking it. Its parent must satisfy the directory contract.
// Existing 0400 legacy files fail closed; never chmod a contender's pathname.
int OpenMozkeyLockFile(std::string_view path);
}  // namespace mozc
#endif
