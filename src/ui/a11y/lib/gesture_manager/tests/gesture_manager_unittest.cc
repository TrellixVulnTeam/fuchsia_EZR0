// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

constexpr uint32_t kDefaultKoid = 100;
constexpr fuchsia::math::PointF kLocalPoint = {2, 2};
constexpr uint32_t kDefaultEventTime = 10;
constexpr uint32_t kDefaultDeviceId = 1;
constexpr uint32_t kDefaultPointerId = 1;

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class GestureManagerTest : public gtest::TestLoopFixture {
 public:
  GestureManagerTest() { SetUp(); }
  void SetUp() override;
  ~GestureManagerTest() override = default;

  a11y::GestureManager gesture_manager_;
  a11y::GestureHandler* gesture_handler_;
  fuchsia::ui::input::accessibility::PointerEventListenerPtr listener_;
  bool up_swipe_detected_ = false;
  bool down_swipe_detected_ = false;
  bool left_swipe_detected_ = false;
  bool right_swipe_detected_ = false;
  bool single_tap_detected_ = false;
  bool double_tap_detected_ = false;
  zx_koid_t actual_viewref_koid_ = 0;
  fuchsia::math::PointF actual_point_ = {.x = 0, .y = 0};
  uint32_t actual_device_id_ = 0;
  uint32_t actual_pointer_id_ = 1000;
};

void GestureManagerTest::SetUp() {
  listener_.Bind(gesture_manager_.binding().NewBinding());
  gesture_handler_ = gesture_manager_.gesture_handler();

  auto up_swipe_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    up_swipe_detected_ = true;
  };

  auto down_swipe_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    down_swipe_detected_ = true;
  };

  auto left_swipe_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    left_swipe_detected_ = true;
  };

  auto right_swipe_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    right_swipe_detected_ = true;
  };

  auto single_tap_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    single_tap_detected_ = true;
  };
  auto double_tap_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    double_tap_detected_ = true;
  };

  // Bind Gestures - Gesture with higher priority should be added first.
  gesture_handler_->BindUpSwipeAction(std::move(up_swipe_callback));
  gesture_handler_->BindDownSwipeAction(std::move(down_swipe_callback));
  gesture_handler_->BindLeftSwipeAction(std::move(left_swipe_callback));
  gesture_handler_->BindRightSwipeAction(std::move(right_swipe_callback));
  gesture_handler_->BindOneFingerDoubleTapAction(std::move(double_tap_callback));
  gesture_handler_->BindOneFingerSingleTapAction(std::move(single_tap_callback));
}

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(kDefaultEventTime);
  event.set_device_id(kDefaultDeviceId);
  event.set_pointer_id(kDefaultPointerId);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_ndc_point({4, 4});
  event.set_viewref_koid(kDefaultKoid);
  event.set_local_point(kLocalPoint);
  return event;
}

void ExecuteOneFingerTapAction(
    fuchsia::ui::input::accessibility::PointerEventListenerPtr* listener) {
  {
    // Send an ADD event.
    auto event = GetDefaultPointerEvent();
    (*listener)->OnEvent(std::move(event));
  }
  {
    // Send a Down event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    (*listener)->OnEvent(std::move(event));
  }
  {
    // Send an UP event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    (*listener)->OnEvent(std::move(event));
  }
  {
    // Send a REMOVE event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    (*listener)->OnEvent(std::move(event));
  }
}

void SendPointerEvents(fuchsia::ui::input::accessibility::PointerEventListenerPtr* listener,
                       const std::vector<PointerParams>& events) {
  for (const auto& event : events) {
    auto pointer_event = ToPointerEvent(event, 0 /*event time (unused)*/);
    pointer_event.set_device_id(kDefaultDeviceId);
    pointer_event.set_pointer_id(kDefaultPointerId);
    pointer_event.set_viewref_koid(kDefaultKoid);
    pointer_event.set_local_point(kLocalPoint);
    (*listener)->OnEvent(std::move(pointer_event));
  }
}  // namespace

TEST_F(GestureManagerTest, CallsActionOnSingleTap) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_TRUE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(up_swipe_detected_);
  EXPECT_FALSE(down_swipe_detected_);
  EXPECT_FALSE(left_swipe_detected_);
  EXPECT_FALSE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnDoubleTap) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  ExecuteOneFingerTapAction(&listener_);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_TRUE(double_tap_detected_);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(up_swipe_detected_);
  EXPECT_FALSE(down_swipe_detected_);
  EXPECT_FALSE(left_swipe_detected_);
  EXPECT_FALSE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnUpSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  // Perform Up Swipe.
  glm::vec2 first_update_ndc_position = {0, -.7f};
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));

  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_TRUE(up_swipe_detected_);
  EXPECT_FALSE(down_swipe_detected_);
  EXPECT_FALSE(left_swipe_detected_);
  EXPECT_FALSE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnDownSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  glm::vec2 first_update_ndc_position = {0, .7f};

  // Perform Down Swipe.
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(up_swipe_detected_);
  EXPECT_TRUE(down_swipe_detected_);
  EXPECT_FALSE(left_swipe_detected_);
  EXPECT_FALSE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnLeftSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  glm::vec2 first_update_ndc_position = {-.7f, 0};

  // Perform Left Swipe.
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(up_swipe_detected_);
  EXPECT_FALSE(down_swipe_detected_);
  EXPECT_TRUE(left_swipe_detected_);
  EXPECT_FALSE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnRightSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  glm::vec2 first_update_ndc_position = {.7f, 0};

  // Perform Right Swipe.
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(up_swipe_detected_);
  EXPECT_FALSE(down_swipe_detected_);
  EXPECT_FALSE(left_swipe_detected_);
  EXPECT_TRUE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, NoGestureDetected) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  // Send an ADD event.
  auto event = GetDefaultPointerEvent();
  listener_->OnEvent(std::move(event));
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(up_swipe_detected_);
  EXPECT_FALSE(down_swipe_detected_);
  EXPECT_FALSE(left_swipe_detected_);
  EXPECT_FALSE(right_swipe_detected_);
}

TEST_F(GestureManagerTest, BindGestureMultipleTimes) {
  auto double_tap_callback = [this](zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid_ = viewref_koid;
    actual_point_ = point;
    double_tap_detected_ = true;
  };
  // Calling Bind again should fail since the gesture is already binded.
  EXPECT_FALSE(gesture_handler_->BindOneFingerDoubleTapAction(std::move(double_tap_callback)));
}

}  // namespace
}  // namespace accessibility_test
