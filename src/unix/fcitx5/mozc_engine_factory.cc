/*
 * Copyright (C) 2020~2020 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see the file COPYING. If not,
 * see <http://www.gnu.org/licenses/>.
 */
#include <fcitx-utils/fs.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>

#include <cstdlib>
#include <string_view>

#include "base/system_util.h"
#include "mozc_engine.h"
#include "unix/fcitx5/i18nwrapper.h"

namespace fcitx {
// This staging build is deliberately fail-closed until asynchronous key
// consumption and preparation lifetime pass A5. No runtime opt-in bypass.
class MozcEngineFactory : public AddonFactory {
 public:
  AddonInstance* create(AddonManager*) override { return nullptr; }
};
}  // namespace fcitx

FCITX_ADDON_FACTORY_V2(mozkey, fcitx::MozcEngineFactory)
