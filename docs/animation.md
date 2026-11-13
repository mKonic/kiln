# Animation

kiln has the Compose animation APIs, from a single animated value to coordinated
transitions between screens. Animations follow the system animation scale, including
"remove animations" in the accessibility settings.

## Animate a value

The `Animate*AsState` functions return a value that moves toward its target whenever the
target changes:

```cpp
auto& open = RememberState(false);
const Dp corner = AnimateDpAsState(open.Get() ? 28_dp : 12_dp);
const Color color = AnimateColorAsState(open.Get() ? Color(0xFF6750A4u) : Color(0xFFE8DEF8u));
Box(SizeMod(Clickable(Background(Modifier{}, color, CornerShape::Rounded(corner)),
                      [&open] { open.Set(!open.Peek()); }), 120_dp, 120_dp));
```

There are versions for `Float`, `Int`, `Dp`, `Color`, `Offset`, `Size`, `Rect`,
`IntOffset` and `IntSize`, and `AnimateValueAsState` for your own types.

## Animation specs

The last argument chooses how the value moves:

| Spec | Motion |
|---|---|
| `anim::Spring(dampingRatio, stiffness)` | A physical spring, the default |
| `anim::Tween(durationMillis, delayMillis, easing)` | A fixed duration along an easing curve |
| `anim::Keyframes(durationMillis, keyframes)` | Values at chosen times |
| `anim::Repeatable(iterations, spec)` | A spec played several times |
| `anim::InfiniteRepeatable(spec)` | A spec played until the call leaves |

```cpp
const float alpha = AnimateFloatAsState(visible ? 1.0f : 0.0f, kiln::anim::Tween(300));
```

## Appear and disappear

`AnimatedVisibility` animates content in and out:

```cpp
Column(Modifier{}, [&open] {
    Text("Tap to open");
    AnimatedVisibility(open.Get(), Modifier{}, [] { Text("More text appears here."); });
});
```

The default fades and expands. Pass other transitions, combined with `+`:
`FadeIn() + ExpandVertically()` and `ShrinkVertically() + FadeOut()`. Slide, scale and
expand transitions exist for both directions.

## Switch between content

`AnimatedContent` animates from one piece of content to the next when its target
changes. `Crossfade` fades between them:

```cpp
AnimatedContent(kiln::rt::Key(count.Get()), Modifier{}, [](kiln::rt::Key k) {
    Text(std::to_string(k.value));
});
```

`AnimateContentSize(modifier)` animates a composable's size when its content changes.

## Transitions

`UpdateTransition` runs several animations from one state, so they start and finish
together:

```cpp
auto& transition = UpdateTransition(selected.Get());
const float scale = AnimateFloat<bool>(transition, [](const bool& on) { return on ? 1.2f : 1.0f; });
```

`RememberInfiniteTransition` repeats animations for as long as the call is in the
composition, for loading indicators and pulses:

```cpp
auto& pulse = RememberInfiniteTransition();
const float alpha = AnimateFloat(pulse, 0.3f, 1.0f,
    kiln::anim::InfiniteRepeatable(kiln::anim::Tween(800), kiln::anim::RepeatMode::Reverse));
```

## Animatable

`Animatable` is a value you animate from coroutines, for gestures that hand off to an
animation:

```cpp
auto& c = *kiln::rt::Composition::Current();
auto offset = c.Remember<std::shared_ptr<kiln::anim::Animatable>>(
    [] { return std::make_shared<kiln::anim::Animatable>(0.0f); });
auto scope = kiln::rt::RememberCoroutineScope();
Button([scope, offset] {
    scope->Launch([](std::shared_ptr<kiln::anim::Animatable> a) -> kiln::rt::Task<> {
        co_await a->AnimateTo(0.0f);
    }(offset));
}, {}, {}, [] { Text("Reset"); });
```

## Shared elements

`SharedTransitionLayout` with the `SharedElement` modifier moves an element from one
screen to the next, as in a list that opens into a detail screen. `NavHost` and
`NavDisplay` provide the animated visibility scope that `SharedElement` needs.
