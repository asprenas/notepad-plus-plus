// toolbarButtonsConf.xml (upstream ToolBar::initHideButtonsConf / ToolBarIcons icoFolderName).
#pragma once

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace npp::core {

struct ToolbarButtonsConfig {
    bool standardHideAll = false;
    std::set<std::pair<int, int>> hiddenStandard; // (index, command id) with hide="yes"
    bool pluginHideAll = false;
    std::vector<bool> pluginHidden;                // by plugin button order
    bool hasIconFolder = false;                    // <ToolBarIcons> present
    std::string iconFolderName;                    // "" = "default"

    // Upstream: a button is hidden if hideAll, or its <Button index id hide="yes"> matches.
    bool isStandardHidden(int index, int commandId) const
    {
        return standardHideAll || hiddenStandard.count({index, commandId}) != 0;
    }

    bool load(std::string_view utf8Path);
    bool loadFromString(std::string_view xml);
};

} // namespace npp::core
