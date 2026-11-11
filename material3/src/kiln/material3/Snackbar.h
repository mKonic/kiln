#pragma once

// Snackbar.kt and SnackbarHost.kt: brief messages at the bottom of the screen with an optional
// action and close button, and the host that queues them (one at a time), dismisses them after
// their duration (as long as accessibility settings ask) and fades and scales them in and out.

#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kiln::ui { class AccessibilityManager; }

namespace kiln::material3 {

    enum class SnackbarResult : std::uint8_t { Dismissed, ActionPerformed };
    enum class SnackbarDuration : std::uint8_t { Short, Long, Indefinite };

    // SnackbarVisuals. Apps may derive their own (as Compose's interface is implemented) and
    // show it by pointer; SnackbarData::Visuals() is then that object (dynamic_cast it back).
    struct SnackbarVisuals {
        std::string message;
        std::optional<std::string> actionLabel;
        // A close button: recommended with SnackbarDuration::Indefinite.
        bool withDismissAction = false;
        SnackbarDuration duration = SnackbarDuration::Short;

        SnackbarVisuals() = default;
        SnackbarVisuals(std::string message, std::optional<std::string> actionLabel = std::nullopt, bool withDismissAction = false,
                        SnackbarDuration duration = SnackbarDuration::Short)
            : message(std::move(message)), actionLabel(std::move(actionLabel)), withDismissAction(withDismissAction), duration(duration) {}
        SnackbarVisuals(const SnackbarVisuals&) = default;
        SnackbarVisuals& operator=(const SnackbarVisuals&) = default;
        virtual ~SnackbarVisuals() = default;
        bool operator==(const SnackbarVisuals&) const = default;
    };

    // One snackbar being shown: its visuals, and how it ends.
    class SnackbarData {
    public:
        virtual ~SnackbarData() = default;
        virtual const SnackbarVisuals& Visuals() const = 0;
        // The action was clicked (the first of these calls decides the result).
        virtual void PerformAction() = 0;
        // Timed out or closed by the user.
        virtual void Dismiss() = 0;
    };

    namespace internal { class SnackbarQueue; }

    // SnackbarHostState: shows at most one snackbar at a time, queueing the rest in order.
    class SnackbarHostState {
    public:
        SnackbarHostState();
        ~SnackbarHostState();
        SnackbarHostState(const SnackbarHostState&) = delete;
        SnackbarHostState& operator=(const SnackbarHostState&) = delete;

        // What the host shows now, or null. Reading it subscribes.
        std::shared_ptr<SnackbarData> CurrentSnackbarData() const { return m_Current.Get(); }

        // Shows (or queues) a snackbar and completes once it is gone, with how it went.
        // Cancelling the job removes it from the screen or the queue. The state must outlive
        // the task. duration unset: Short without an action label, Indefinite with one.
        rt::Task<SnackbarResult> ShowSnackbar(std::string message, std::optional<std::string> actionLabel = std::nullopt,
                                              bool withDismissAction = false, std::optional<SnackbarDuration> duration = std::nullopt);
        rt::Task<SnackbarResult> ShowSnackbar(SnackbarVisuals visuals);
        rt::Task<SnackbarResult> ShowSnackbar(std::shared_ptr<const SnackbarVisuals> visuals);

    private:
        rt::State<std::shared_ptr<SnackbarData>> m_Current{ nullptr };
        std::unique_ptr<internal::SnackbarQueue> m_Queue;
    };
    // remember { SnackbarHostState() }
    SnackbarHostState& RememberSnackbarHostState(rt::Key key = {});

    namespace SnackbarDefaults {
        foundation::ShapePtr shape();
        ui::Color color();
        ui::Color contentColor();
        ui::Color actionColor();
        ui::Color actionContentColor();
        ui::Color dismissActionContentColor();
    }

    struct SnackbarOptions {
        std::function<void()> action;          // a button; consider colorScheme.inversePrimary
        std::function<void()> dismissAction;   // a close button for snackbars that do not time out
        bool actionOnNewLine = false;          // for long action labels
        foundation::ShapePtr shape;            // null: SnackbarDefaults::shape
        std::optional<ui::Color> containerColor, contentColor, actionContentColor, dismissActionContentColor;
    };
    // The snackbar's visuals with any content.
    void Snackbar(const ui::Modifier& modifier, const SnackbarOptions& options, std::function<void()> content);

    struct SnackbarDataOptions {
        bool actionOnNewLine = false;
        foundation::ShapePtr shape;
        std::optional<ui::Color> containerColor, contentColor, actionColor, actionContentColor, dismissActionContentColor;
    };
    // The snackbar for `data` (from a SnackbarHost): its message, a text button for its action
    // and a close button with a tooltip when it asks for one; 12 dp of padding around it.
    void Snackbar(std::shared_ptr<SnackbarData> data, const ui::Modifier& modifier = {}, const SnackbarDataOptions& options = {});

    // SnackbarHost: shows hostState's snackbars with `snackbar` (null: the default Snackbar).
    void SnackbarHost(SnackbarHostState& hostState, const ui::Modifier& modifier = {},
                      std::function<void(std::shared_ptr<SnackbarData>)> snackbar = {});

    namespace internal {
        // SnackbarDuration.toMillis: INT64_MAX for Indefinite; `manager` (may be null) stretches
        // it to the user's preference.
        std::int64_t SnackbarDurationMillis(SnackbarDuration duration, bool hasAction, const ui::AccessibilityManager* manager);
    }

}
