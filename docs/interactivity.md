# Interactivity and text input

This page covers taps, gestures, keyboard focus, text fields and accessibility.

## Taps

`Clickable` makes any composable respond to taps, with a ripple and the accessibility
click action:

```cpp
auto& liked = RememberState(false);
Box(Clickable(Padding(Modifier{}, 12_dp), [&liked] { liked.Set(!liked.Peek()); }), Alignment::Center(), false,
    [&liked] { Text(liked.Get() ? "Liked" : "Like"); });
```

For something that is on or off, `Toggleable(m, value, onValueChange)` also reports its
state to accessibility services. `Selectable(m, selected, onClick)` does the same for one
choice in a group.

## Gestures

`PointerInput` runs a gesture detector as a coroutine. It restarts when its keys change:

```cpp
PointerInput(Modifier{}, kiln::rt::Keys{}, [](PointerInputScope& scope) {
    return DetectTapGestures(scope, {
        .onDoubleTap = [](Offset at) { /* zoom in at `at` */ },
        .onLongPress = [](Offset) { /* show a menu */ },
    });
});
```

| Detector | Recognizes |
|---|---|
| `DetectTapGestures` | Tap, double tap, long press, press and release |
| `DetectDragGestures` | Drags in any direction |
| `DetectHorizontalDragGestures`, `DetectVerticalDragGestures` | Drags along one axis |

### Dragging, zooming and rotating

For common gestures there are modifiers with their own state:

```cpp
auto& scale = RememberState(1.0f);
auto& rotation = RememberState(0.0f);
auto& offset = RememberState(Offset{});
auto& state = RememberTransformableState([&](Offset, float zoom, Offset pan, float rotate) {
    scale.Set(scale.Peek() * zoom);
    rotation.Set(rotation.Peek() + rotate);
    offset.Set(offset.Peek() + pan);
});
GraphicsLayerParams layer;
layer.scaleX = layer.scaleY = scale.Get();
layer.rotationZ = rotation.Get();
layer.translationX = offset.Get().x;
layer.translationY = offset.Get().y;
Box(Transformable(GraphicsLayer(FillMax(Modifier{}, Axis::Both), layer), state));
```

`Transformable` handles two-finger pinch and rotation on a touch screen, and pinch and
two-finger scroll on a trackpad. `Draggable(m, state, orientation)` handles a drag along
one axis, with `RememberDraggableState(onDelta)`.

## Focus and keys

Composables that take input are focusable: tab, arrow keys and a D-pad move between them.
Make anything focusable with `Focusable(m)`, and handle hardware keys with
`OnKeyEvent(m, handler)`, where the handler returns `true` when it used the key.

To move focus from code, attach a `FocusRequester`:

```cpp
auto& c = *kiln::rt::Composition::Current();
auto& requester = c.Remember<FocusRequester>([] { return FocusRequester(); });
TextField(name, FocusRequesterModifier(Modifier{}, requester));
// later, for example in an effect:
requester.RequestFocus();
```

## Text fields

A text field edits a `TextFieldState`, which holds the text and selection:

```cpp
auto& name = RememberTextFieldState();
Column(Padding(Modifier{}, 16_dp), [&name] {
    TextFieldOptions options;
    options.label = [](const TextFieldLabelScope&) { Text("Name"); };
    options.lineLimits = TextFieldLineLimits::SingleLine();
    options.keyboardOptions.imeAction = ImeAction::Done;
    OutlinedTextField(name, Modifier{}, options);
    Text("Hello, " + name.TextUtf8());
});
```

* `TextField` is the filled style and `OutlinedTextField` the outlined one.
  `SecureTextField` and `OutlinedSecureTextField` hide what is typed.
* `keyboardOptions` chooses the keyboard: `keyboardType` (`Number`, `Email`, `Phone`
  and others), `imeAction`, `capitalization` and `autoCorrectEnabled`.
* `onKeyboardAction` runs when the action key is pressed.
* `inputTransformation` filters or changes what is typed, for example to allow only
  digits.

Text fields work with the system keyboard, including composition, handwriting with a
stylus, autofill, and the text toolbar with cut, copy, paste and the actions other apps
add to it.

## Accessibility

kiln reports the UI to TalkBack, Switch Access and other accessibility services from
its semantics tree. Components and modifiers such as `Clickable` and `Toggleable` fill
it in. Describe custom content with `Semantics`:

```cpp
Box(Semantics(SizeMod(Modifier{}, 48_dp, 48_dp), [](SemanticsConfiguration& c) {
    c.contentDescription = { "Profile picture" };
    c.role = Role::Image;
}));
```

`ClearAndSetSemantics` replaces what the children report, and the `mergeDescendants`
argument of `Semantics` reads a group as one element.
