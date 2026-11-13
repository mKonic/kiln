# Composables, state and effects

A kiln UI is a tree of composable calls. When state they read changes, kiln runs the
affected calls again and updates the screen. This is called recomposition.

## Composables

A composable is a function that emits UI by calling other composables:

```cpp
void Greeting(const std::string& name) {
    Text("Hello, " + name);
}
```

Call composables only inside content: the lambda given to `kiln::SetContent`, to a
layout or to a component.

## State

`RememberState` creates a value that survives recomposition:

```cpp
void Counter() {
    auto& count = RememberState(0);
    Button([&count] { count.Set(count.Peek() + 1); }, {}, {}, [&count] {
        Text("Pressed " + std::to_string(count.Get()) + " times");
    });
}
```

| Call | Effect |
|---|---|
| `Get()` | Reads the value and subscribes the current scope to it |
| `Peek()` | Reads the value without subscribing |
| `Set(v)` | Writes the value and schedules the scopes that read it |
| `Update(fn)` | Changes the value in place, then schedules those scopes |

Read with `Get()` while composing and with `Peek()` in event handlers.

### State that survives process death

`kiln::rt::RememberSaveableState` keeps its value across configuration changes and
across the app being killed in the background, as `rememberSaveable` does:

```cpp
auto& tab = kiln::rt::RememberSaveableState(0);
```

### Remembering other objects

`Remember` keeps any object for as long as the call stays in the composition:

```cpp
auto& c = *kiln::rt::Composition::Current();
auto& cache = c.Remember<std::vector<std::string>>([] { return std::vector<std::string>{}; });
```

Pass keys to create the object again when they change:
`c.Remember<T>(init, kiln::rt::Keys{ userId })`.

## Effects

Effects run code that is not UI: timers, subscriptions, loading data.

### LaunchedEffect

Starts a coroutine when the call enters the composition, and cancels it when the call
leaves or its keys change:

```cpp
void Clock() {
    auto& seconds = RememberState(0);
    auto& c = *kiln::rt::Composition::Current();
    c.LaunchedEffect(kiln::rt::Keys{}, [&seconds]() -> kiln::rt::Task<> {
        for (;;) {
            co_await kiln::rt::Delay(1000);
            seconds.Set(seconds.Peek() + 1);
        }
    });
    Text("Running for " + std::to_string(seconds.Get()) + " s");
}
```

### DisposableEffect

Runs setup when the call enters the composition and the returned cleanup when it leaves:

<!-- prelude: auto& c = *kiln::rt::Composition::Current(); std::string topic; void Subscribe(const std::string&); void Unsubscribe(const std::string&); -->
```cpp
c.DisposableEffect(kiln::rt::Keys{ topic }, [topic] {
    Subscribe(topic);
    return kiln::Function<void()>([topic] { Unsubscribe(topic); });
});
```

### SideEffect

`c.SideEffect(fn)` runs `fn` after every successful recomposition of the call.

### Coroutines from events

To start a coroutine from a click handler, remember a scope:

<!-- prelude: LazyListState& listState = RememberLazyListState(); -->
```cpp
auto scope = kiln::rt::RememberCoroutineScope();
Button([scope, &listState] { scope->Launch(listState.AnimateScrollToItem(0)); }, {}, {},
       [] { Text("Back to top"); });
```

## Lifetimes

Content lambdas can run again after the function that created them has returned.
Capture remembered state by reference, and everything else by value. See
[Lambdas and lifetimes](compose-developers.md#lambdas-and-lifetimes).
