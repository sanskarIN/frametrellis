// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/edit_command.h"

#include <memory>
#include <vector>

namespace ft {

/// Linear undo/redo stack with a clean marker (the saved state) and a size limit.
class CommandHistory
{
public:
    explicit CommandHistory(int limit = 500);

    /// Performs the command. On failure the project is unchanged and nothing is recorded.
    /// Commands that change nothing are not recorded either.
    Result<ChangeSet> execute(std::unique_ptr<EditCommand> command, Project& project);
    Result<ChangeSet> undo(Project& project);
    Result<ChangeSet> redo(Project& project);

    [[nodiscard]] bool canUndo() const { return m_index > 0; }
    [[nodiscard]] bool canRedo() const { return m_index < count(); }
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;
    [[nodiscard]] int count() const { return static_cast<int>(m_commands.size()); }
    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] int limit() const { return m_limit; }

    [[nodiscard]] bool isClean() const { return m_cleanIndex == m_index; }
    void setClean() { m_cleanIndex = m_index; }
    void clear();

    /// Ends the current merge sequence so that the next command starts a new undo step.
    void breakMerge() { m_mergeBlocked = true; }

private:
    std::vector<std::unique_ptr<EditCommand>> m_commands;
    int m_index = 0;
    int m_cleanIndex = 0; ///< -1 when the saved state is no longer reachable
    int m_limit;
    bool m_mergeBlocked = false;
};

} // namespace ft
