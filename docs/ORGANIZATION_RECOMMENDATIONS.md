# Repository Organization Recommendations

This document outlines recommendations for improving the organization and maintainability of the Blocks o' Code v3 repository.

## 🚨 Critical Issues

### 1. Flutter App: Monolithic `main.dart` File

**Current State:**
- `companion_app/lib/main.dart` was **2,317 lines** long (now refactored)
- Contains multiple screen classes, widgets, and business logic all in one file

**Recommendation:**
Break down `main.dart` into a proper screen/widget structure:

```
companion_app/lib/
├── main.dart                    # Entry point only (~11 lines) ✓
├── app/
│   └── blocks_of_code_app.dart  # App widget with theme config ✓
├── screens/
│   ├── welcome_screen.dart      ✓
│   ├── about_screen.dart        ✓
│   ├── block_config_screen.dart ✓
│   ├── settings_screen.dart      ✓
│   ├── help_screen.dart          ✓
│   └── tutorial_screen.dart     ✓
├── widgets/
│   ├── block_3d_visualizer.dart  ✓
│   ├── hero_blocks_strip.dart   ✓
│   ├── hero_cube.dart           ✓
│   ├── onboarding_steps_row.dart ✓
│   └── step_chip.dart           ✓
├── models/                      # Already well-organized ✓
├── services/                    # Already well-organized ✓
├── providers/                   # State management ✓
├── config/                     # Configuration ✓
└── utils/
    └── navigation.dart         # ScreenType enum ✓
```

**Benefits:**
- Easier to navigate and find code
- Better code reusability
- Improved testability
- Reduced merge conflicts
- Better IDE performance

### 2. Git: Cache Files Being Tracked

**Current State:**
- `.cache/clangd/index/*.idx` files are showing as modified in git status
- These are IDE cache files and should not be tracked

**Recommendation:**
Add to `.gitignore`:
```
# IDE caches
.cache/
*.idx
**/.cache/
```

## 📁 Directory Structure Improvements

### 3. Empty Placeholder Directories

**Current State:**
- `scripts/` and `tools/` directories only contain `.gitkeep` files

**Recommendation:**
Either:
- **Option A:** Remove empty directories if not needed
- **Option B:** Add initial README.md files explaining their intended purpose

If keeping them, add:
```
scripts/README.md
tools/README.md
```

### 4. Firmware Organization

**Current State:**
- Multiple firmware projects: `brain_block`, `child_block_1`, `child_block_2`, `block_templates`
- Structure is reasonable but could benefit from better documentation

**Recommendation:**
Create a firmware organization guide:

```
firmware_blocks/
├── README.md                    # Already exists ✓
├── FRAMEWORK.md                 # Already exists ✓
├── brain_block/
│   └── README.md                ✓
├── child_block_1/
│   └── README.md                ✓
├── child_block_2/
│   └── README.md                ✓
├── block_templates/
└── include/
    └── i2c_protocol.h           # Shared protocol header ✓
```

**Note:** `firmware_blocks/include/i2c_protocol.h` appears to be shared - consider moving to a `common/` directory.

### 5. Documentation Structure

**Current State:**
- Documentation exists but is scattered
- Some docs are in root, some in `docs/`, some in subdirectories

**Recommendation:**
Consolidate documentation:

```
docs/
├── README.md                    # Documentation index
├── getting-started/
│   ├── overview.md
│   ├── firmware-setup.md        # Move ESP-IDF-setup.md here
│   └── app-setup.md
├── architecture/
│   ├── system-overview.md
│   ├── firmware-architecture.md
│   └── app-architecture.md
├── development/
│   ├── contributing.md
│   ├── code-style.md
│   └── testing.md
├── hardware/
│   └── block-inventory.md       # Move BLOCK_INVENTORY.md here
└── api/
    ├── firmware-api.md
    └── app-api.md
```

## 🔧 Code Organization Improvements

### 6. Flutter App: Service Layer Enhancement

**Current State:**
- Services exist but could be more modular
- TCP server logic is embedded in `MainScreen`

**Recommendation:**
Extract TCP server into a dedicated service:

```
lib/services/
├── tcp_server_service.dart      # NEW: TCP server management
├── connection_manager.dart       # NEW: Connection state management
├── block_config_parser.dart      # Already exists ✓
├── configuration_validator.dart  # Already exists ✓
└── telemetry_parser.dart         # Already exists ✓
```

### 7. Flutter App: State Management

**Current State:**
- All state is managed in `_MainScreenState`
- No clear separation of concerns

**Recommendation:**
Consider using a state management solution:
- **Provider** (simple, recommended for this project)
- **Riverpod** (more modern, type-safe)
- **Bloc** (if you need complex state machines)

This would allow:
- Separation of UI and business logic
- Better testability
- Easier state sharing between screens

### 8. Constants and Configuration

**Current State:**
- Magic numbers and strings scattered throughout code
- TCP port `41233` hardcoded in multiple places

**Recommendation:**
Create a constants file:

```
lib/config/
├── app_config.dart              # App-wide configuration
└── constants.dart               # Constants (ports, timeouts, etc.)
```

Example:
```dart
class AppConfig {
  static const int tcpPort = 41233;
  static const Duration heartbeatInterval = Duration(seconds: 30);
  static const Duration heartbeatTimeout = Duration(seconds: 60);
  // etc.
}
```

## 📋 Testing Structure

### 9. Add Test Directories

**Current State:**
- No test directories visible

**Recommendation:**
Add test structure:

```
companion_app/
├── test/
│   ├── unit/
│   │   ├── models/
│   │   ├── services/
│   │   └── widgets/
│   ├── integration/
│   └── test_helpers/
└── integration_test/
```

## 🗂️ Asset Organization

### 10. Flutter Assets

**Current State:**
- Assets exist but structure could be clearer

**Recommendation:**
Organize assets by type:

```
assets/
├── fonts/
├── images/
│   ├── icons/
│   ├── blocks/
│   └── ui/
├── configs/                     # Rename from root level
│   ├── sample_block_config.json
│   └── sample_block_config_invalid.json
└── animations/                  # If you add animations later
```

## 🔐 Security and Configuration

### 11. Environment Configuration

**Recommendation:**
Create environment-specific configuration:

```
lib/config/
├── environments/
│   ├── dev.dart
│   ├── prod.dart
│   └── staging.dart
└── environment.dart             # Current environment selector
```

This allows different TCP ports, timeouts, etc. for different environments.

### 12. Secrets Management

**Recommendation:**
- Create `.env.example` file showing required environment variables
- Add `.env` to `.gitignore`
- Use `flutter_dotenv` package for loading secrets

## 📊 Build and CI/CD

### 13. Build Scripts

**Recommendation:**
Add build automation:

```
scripts/
├── build_firmware.sh            # Build all firmware projects
├── build_app.sh                 # Build Flutter app
├── flash_firmware.sh            # Flash firmware to device
└── run_tests.sh                 # Run all tests
```

### 14. CI/CD Configuration

**Recommendation:**
Add CI/CD configuration files:

```
.github/
├── workflows/
│   ├── firmware-ci.yml          # Build and test firmware
│   ├── app-ci.yml               # Build and test Flutter app
│   └── integration-test.yml     # Integration tests
```

## 🏷️ Version Management

### 15. Version Files

**Recommendation:**
Create version tracking:

```
VERSION                          # Simple version file
CHANGELOG.md                     # Detailed changelog
```

Update these with each release.

## 📝 Code Quality

### 16. Linting and Formatting

**Current State:**
- `analysis_options.yaml` exists ✓

**Recommendation:**
- Ensure strict linting rules are enabled
- Add `dart format` check to CI/CD
- Consider adding `dart_code_metrics` for additional analysis

### 17. Documentation Comments

**Recommendation:**
- Add dartdoc comments to all public APIs
- Generate API documentation: `dart doc`
- Host generated docs (GitHub Pages, etc.)

## 🎯 Priority Implementation Order

1. **High Priority:**
   - [ ] Break down `main.dart` into separate screen files
   - [ ] Fix `.gitignore` to exclude cache files
   - [ ] Extract TCP server into service class

2. **Medium Priority:**
   - [ ] Add constants/config files
   - [ ] Organize documentation structure
   - [ ] Add test directories and initial tests

3. **Low Priority:**
   - [ ] Add build scripts
   - [ ] Set up CI/CD
   - [ ] Add environment configuration

## 📚 Additional Resources

- [Flutter Best Practices](https://docs.flutter.dev/development/best-practices)
- [Dart Style Guide](https://dart.dev/guides/language/effective-dart/style)
- [ESP-IDF Project Structure](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html)

---

**Last Updated:** February 12, 2026
