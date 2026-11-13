# Material 3 components and theming

`kiln::material3` is a port of Compose Material 3, including the expressive components
and motion. The [gallery sample](https://github.com/mKonic/kiln/tree/master/samples/gallery) shows every component.

## Theme

Wrap the app in `MaterialTheme`. Components read their colors, type, shapes and motion
from it:

```cpp
kiln::SetContent(app, [] { MaterialTheme([] { App(); }); });
```

`MaterialThemeArgs` replaces parts of the theme. Unset fields keep the enclosing theme's:

```cpp
void AppTheme(kiln::Function<void()> content) {
    const Configuration& config = kiln::rt::Composition::Current()->Read(LocalConfiguration);
    const bool dark = IsSystemInDarkTheme();
    std::optional<ColorScheme> scheme = dark ? DynamicDarkColorScheme(config) : DynamicLightColorScheme(config);
    if (!scheme) scheme = dark ? DarkColorScheme() : LightColorScheme();
    MaterialTheme({ .colorScheme = scheme }, std::move(content));
}
```

Read the theme inside content with `MaterialTheme::colorScheme()`,
`MaterialTheme::typography()`, `MaterialTheme::shapes()` and
`MaterialTheme::motionScheme()`.

### Color

| Function | Scheme |
|---|---|
| `LightColorScheme()`, `DarkColorScheme()` | The Material 3 baseline |
| `DynamicLightColorScheme(config)`, `DynamicDarkColorScheme(config)` | From the user's wallpaper |
| `ColorSchemeFromSeed(color, dark, variant)` | Generated from one color |

The baseline functions take a callback that changes individual roles:
`LightColorScheme([](ColorScheme& c) { c.primary = Color(0xFF00658Fu); })`.

### Type, shapes and motion

* `Typography` holds the type scale, from `displayLarge` to `labelSmall`, with the
  emphasized styles.
* `Shapes` holds the corner sizes from `extraSmall` to `extraLarge`. `MaterialShapes`
  has the expressive shapes, such as cookies and clovers, which morph into each other.
* `MotionScheme::Standard()` and `MotionScheme::Expressive()` set the springs that
  components animate with.

## Components

| Group | Components |
|---|---|
| Actions | `Button`, `ElevatedButton`, `FilledTonalButton`, `OutlinedButton`, `TextButton`, `IconButton`, `FloatingActionButton`, `ExtendedFloatingActionButton`, `FloatingActionButtonMenu`, `ButtonGroup`, `SplitButton`, `ToggleButton`, `SegmentedButton` |
| Selection | `Checkbox`, `RadioButton`, `Switch`, `Slider`, `RangeSlider`, chips |
| Text input | `TextField`, `OutlinedTextField`, `SecureTextField`, `SearchBar`, `ExposedDropdownMenu` |
| Containers | `Card`, `ListItem`, `Surface`, `Divider`, `Carousel`, `SwipeToDismissBox` |
| Navigation | `TopAppBar` and its variants, `NavigationBar`, `ShortNavigationBar`, `NavigationRail`, `WideNavigationRail`, `NavigationDrawer`, `TabRow`, `FloatingToolbar` |
| Layout | `Scaffold`, `BottomSheetScaffold`, `NavigationSuiteScaffold`, `ThreePaneScaffold` |
| Dialogs and sheets | `AlertDialog`, `ModalBottomSheet`, `DatePicker`, `DateRangePicker`, `TimePicker`, `Menu`, `Tooltip` |
| Feedback | `Snackbar`, `Badge`, `ProgressIndicator`, `WavyProgressIndicator`, `LoadingIndicator`, `PullToRefresh` |
| Text and icons | `Text`, `Icon` |

Components take their required arguments first, then a modifier, then an options struct
for everything else, then their content:

```cpp
ButtonOptions options;
options.enabled = isValid;
Button([&] { Submit(); }, Modifier{}, options, [] { Text("Submit"); });
```

## Icons

`Icon` draws an `ImageVector`, tinted with the content color. Build one from SVG path
data, such as a Material Symbols icon:

```cpp
const kiln::gfx::vector::ImageVectorPtr& AddIcon() {
    static const auto icon = kiln::gfx::vector::ImageVector::Builder("Add", 24_dp, 24_dp, 960, 960)
        .Path(kiln::gfx::vector::ParsePathString("M440-440H200v-80h240v-240h80v240h240v80H520v240h-80v-240Z"),
              kiln::gfx::Brush::SolidColor(Color(0xFF000000u)))
        .Build();
    return icon;
}

IconButton([] { AddItem(); }, Modifier{}, {}, [] { Icon(AddIcon(), std::string("Add")); });
```

The second argument of `Icon` is its description for accessibility services. Pass
`std::nullopt` for decorative icons.

## Images

Put images in the app's assets folder (`ASSETS` in the app's `CMakeLists.txt`), then decode
them and draw them with `Image`:

```cpp
auto& c = *kiln::rt::Composition::Current();
const auto& photo = c.Remember<kiln::gfx::ImageBitmap>([] {
    auto bytes = kiln::ReadAsset("photos/beach.jpg");
    return bytes ? kiln::gfx::ImageBitmap::Decode(*bytes) : kiln::gfx::ImageBitmap{};
});
Image(photo, std::string("A beach at sunset"), FillMax(Modifier{}, Axis::Horizontal),
      Alignment::Center(), ContentScale::Crop());
```

PNG, JPEG, WebP and HEIF decode on Android, and GIF decodes its first frame.
