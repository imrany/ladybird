/*
 * Copyright (c) 2022, the SerenityOS developers.
 * Copyright (c) 2024, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2024, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/AnimationFrameCallbackDriver.h>

namespace Web::HTML {

JS_DEFINE_ALLOCATOR(AnimationFrameCallbackDriver);

void AnimationFrameCallbackDriver::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_callbacks);
}

WebIDL::UnsignedLong AnimationFrameCallbackDriver::add(Callback handler)
{
    auto id = ++m_animation_frame_callback_identifier;
    m_callbacks.set(id, handler);
    return id;
}

bool AnimationFrameCallbackDriver::remove(WebIDL::UnsignedLong id)
{
    return m_callbacks.remove(id);
}

bool AnimationFrameCallbackDriver::has_callbacks() const
{
    return !m_callbacks.is_empty();
}

void AnimationFrameCallbackDriver::run(double now)
{
    auto taken_callbacks = move(m_callbacks);
    for (auto& [id, callback] : taken_callbacks)
        callback->function()(now);
}

}