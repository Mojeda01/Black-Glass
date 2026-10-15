#pragma once
#include "Level.h"

#include <cstddef>
#include <deque>
#include <utility>

class History
{
public:
    explicit History(size_t limit = 100)
        : m_limit(limit)
    {
    }

    bool ready() const { return m_ready; }
    size_t undoCount() const { return m_undo.size(); }
    size_t redoCount() const { return m_redo.size(); }

    void reset(LevelData state)
    {
        m_undo.clear();
        m_redo.clear();
        m_current = std::move(state);
        m_ready = true;
    }

    void setCurrent(LevelData state)
    {
        m_current = std::move(state);
        m_ready = true;
    }

    bool observe(LevelData state)
    {
        if (!m_ready)
        {
            reset(std::move(state));
            return false;
        }
        if (sameContent(state, m_current))
        {
            m_current = std::move(state);
            return false;
        }
        m_undo.push_back(std::move(m_current));
        if (m_undo.size() > m_limit)
        {
            m_undo.pop_front();
        }
        m_redo.clear();
        m_current = std::move(state);
        return true;
    }

    bool undo(LevelData& target)
    {
        if (m_undo.empty())
        {
            return false;
        }
        m_redo.push_back(m_current);
        target = std::move(m_undo.back());
        m_undo.pop_back();
        return true;
    }

    bool redo(LevelData& target)
    {
        if (m_redo.empty())
        {
            return false;
        }
        m_undo.push_back(m_current);
        target = std::move(m_redo.back());
        m_redo.pop_back();
        return true;
    }

private:
    size_t m_limit;
    bool m_ready = false;
    LevelData m_current;
    std::deque<LevelData> m_undo;
    std::deque<LevelData> m_redo;
};
