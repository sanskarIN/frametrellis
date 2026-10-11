// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/command_history.h"

#include "core/logging.h"

#include <algorithm>

namespace ft {

CommandHistory::CommandHistory(int limit) : m_limit(std::max(1, limit)) {}

Result<ChangeSet> CommandHistory::execute(std::unique_ptr<EditCommand> command, Project& project)
{
    if (!command) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("No command to execute."));
    }
    Result<ChangeSet> result = command->redo(project);
    const bool blocked = m_mergeBlocked;
    m_mergeBlocked = false;
    if (!result) {
        qCInfo(lcEditing) << "Edit rejected:" << command->text() << result.error().toString();
        return result;
    }
    if (result->isEmpty()) {
        return result;
    }

    // Executing a new command discards everything that could have been redone.
    m_commands.erase(m_commands.begin() + m_index, m_commands.end());
    if (m_cleanIndex > m_index) {
        m_cleanIndex = -1;
    }

    const QString key = command->mergeKey();
    if (!blocked && !key.isEmpty() && m_index > 0 && m_cleanIndex != m_index) {
        EditCommand& top = *m_commands[static_cast<size_t>(m_index - 1)];
        if (top.mergeKey() == key && top.mergeWith(*command)) {
            return result;
        }
    }

    m_commands.push_back(std::move(command));
    ++m_index;
    while (count() > m_limit) {
        m_commands.erase(m_commands.begin());
        --m_index;
        m_cleanIndex = m_cleanIndex > 0 ? m_cleanIndex - 1 : -1;
    }
    return result;
}

Result<ChangeSet> CommandHistory::undo(Project& project)
{
    if (!canUndo()) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("There is nothing to undo."));
    }
    m_mergeBlocked = true;
    --m_index;
    return m_commands[static_cast<size_t>(m_index)]->undo(project);
}

Result<ChangeSet> CommandHistory::redo(Project& project)
{
    if (!canRedo()) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("There is nothing to redo."));
    }
    m_mergeBlocked = true;
    Result<ChangeSet> result = m_commands[static_cast<size_t>(m_index)]->redo(project);
    if (!result) {
        qCWarning(lcEditing) << "Redo failed unexpectedly:" << result.error().toString();
        return result;
    }
    ++m_index;
    return result;
}

QString CommandHistory::undoText() const
{
    return canUndo() ? m_commands[static_cast<size_t>(m_index - 1)]->text() : QString();
}

QString CommandHistory::redoText() const
{
    return canRedo() ? m_commands[static_cast<size_t>(m_index)]->text() : QString();
}

void CommandHistory::clear()
{
    m_commands.clear();
    m_index = 0;
    m_cleanIndex = 0;
    m_mergeBlocked = false;
}

} // namespace ft
