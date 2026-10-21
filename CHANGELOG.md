# Changelog

Changes for each templide release. Entries follow the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format.

## [1.4.1] - 2026-10-20

Changes since v1.4.0. This release adds agent profiles to the AI tab, and the AI tab can now run local models from Ollama or LM Studio through OpenCode.

### Added

#### AI Agent

- **Agent profiles**: A profile combines an agent program, a model provider, and a model. Manage profiles on the `AI Agent` page in Settings, and pick one from the dropdown in the AI tab. Each file tab runs each profile in its own session.
- **Default profiles**: `Claude Code` (Anthropic) and `Codex` (OpenAI) are included and use each agent's default model. They can be edited or deleted, and `Restore default profiles` brings them back without touching other profiles.
- **Model selection**: Claude Code receives the profile's model through `--model` and Codex through `-m`. Leave the model empty to use the agent's default. Claude Code profiles suggest `sonnet`, `opus`, and `haiku`.
- **Local models through OpenCode**: OpenCode profiles run models on this computer with Ollama or LM Studio.
  - Ollama's address can be set per profile (default `http://localhost:11434`). The model list shows only installed Ollama models that support tool calling.
  - LM Studio uses its local server at `http://127.0.0.1:1234`.
  - A context length of 64K or more is recommended.
  - Starting a local-model profile without a model shows a message with an `Open settings` button.
- **OpenCode 1.x and 2.x**: The editor runs `opencode --version` before launching OpenCode.
  - Settings and the MCP connection are passed through the `OPENCODE_CONFIG_CONTENT` environment variable. OpenCode's configuration files are not modified.
  - 2.x runs with `--standalone`, so it does not join a background server that still holds the MCP address of an earlier editor run.
  - Code Mode is turned off for the editor's MCP server, so small models call the editor's tools directly.
- Claude Code and Codex use only their own providers. Sign-in and billing happen inside the agent with your own account, and the editor does not handle API keys.

### Changed

- **`AI Agent` settings page**:
  - Profiles are shown as a collapsible list. Click a row to edit it; only one profile is expanded at a time.
  - Click the bold name of an expanded profile to rename it. Profiles without a name are named after their combination, such as `Claude Code` or `OpenCode · Ollama · gemma4:e4b`. The provider is omitted when the agent has only one.
  - Rows with a problem, such as a missing model, show a warning icon.
- **Default profile**: The `Default agent` dropdown was replaced by a `Default` badge in the profile list; other profiles have a `Make default` button. The previously selected default agent becomes the default profile.
- **Executable paths**: Each agent's executable path, now including OpenCode, takes one line. An empty field shows which program is looked up in `PATH`.

## [1.4.0] - 2026-10-20

Changes since v1.3.0. This release adds charts, equations, a code formatter, accessibility checks, and diagnostic codes, as well as dedicated editing views for `template` and `master` definitions.

### Breaking Changes

- **Declare `master` parameters with `var`.** The previous `master brand(color ink) { ... }` syntax is no longer supported. Declare parameters as `var` lines inside `{}`, like `template` definitions; parameters may have default values.

  ```tlide
  master brand {
      var color ink = hex(222222);
      var string name;
      case title { /* ... */ }
  }
  ```

  Parameters with default values may be omitted or passed by name: `brand(name = "x").title`, `brand(hex(FF0000), name = "x").title`. Defaults must be constants, and parameters without defaults are required. Named arguments (`name = value`) are only supported in layout master calls and must come after positional arguments.
- **Command-line exit codes have changed.** Invalid commands and file I/O failures now exit with `2` instead of `1`. Exit code `1` is now reserved for compilation errors (and warnings when `--strict` is enabled). Exit code `0` still indicates success. Update scripts or CI pipelines that inspect exit codes.

### Added

#### Language and Output

- **`chart` object**: Define charts directly in code. Supported types are column, bar, line, area, pie, and scatter charts, with options for stacking, smooth lines, markers, data labels, legend position, value-axis ranges, and series colors. PPTX output contains editable PowerPoint charts with embedded workbooks; HTML, PDF, and PNG output renders the same charts directly.
- **`math` object**: Write equations in LaTeX. PPTX output uses editable PowerPoint equations (OMML); HTML, PDF, and PNG output uses KaTeX 0.16.11 from `libs/katex`. Fractions, roots, subscripts, large operators, limits, function names, Greek letters and symbols, accents, matrices, and `cases` are supported.
- **More flexible `master` calls**: Parameters with defaults may be omitted or passed by name (see “Breaking Changes” above).
- **`#fmt option value` lines**: Configure the code formatter without changing the meaning of the document (see “Code Formatter” below).

#### Diagnostic Codes and Diagnostics

- All errors and warnings now have **codes**: `E1xxx` for syntax, `E2xxx` for analysis, `E3xxx` for command-line errors, `E4xxx` for formatter errors, `W…` for warnings, and `A0001`–`A0008` for accessibility issues. Once assigned, codes will not change. The catalog of codes and message formats is in `middleend/diagnostic_catalog.cpp`, and `test/catalog_check.py` verifies that call sites use the correct entries.
- Misspelled names now produce suggestions in a `help:` line.
- LaTeX syntax errors in equations (`E2172`) are always errors. Commands that cannot be converted to PowerPoint equations (`E2173`) are errors only when building a `pptx` document. Invalid chart data and empty equations are also reported.
- Diagnostics are always printed in English. The Korean/English editor UI switch is unchanged.

#### Command Line

- `--format text|json|github`: Select the diagnostic output format. The default is gcc-style `file:line:column: error[E2117]: message`; `json` is intended for tools, and `github` emits GitHub Actions `::error file=...::` annotations.
- `--strict`: Treat warnings as failures (exit code 1).
- `--check --fmt`: Check whether files containing `#fmt` are formatted according to those settings; otherwise, emit warning `W4001`.
- `--check --access`: Run accessibility checks as well.
- `templide fmt <file>…`: Run the code formatter (`--check`, `--stdout`, and `--format`).
- `templide tool access <file>`: Run only the accessibility checker (`--format`, `--strict`).

#### Code Formatter

- `templide fmt` and the editor toolbar’s magic-wand button (Alt+Shift+F) run the formatter. Settings are written as `#fmt name value` lines in the `.tlide` file, and **files without any `#fmt` lines are not modified**.
- 21 options are supported: indentation (`indent_style`, `indent_size`), brace placement (`brace_style`), single-line blocks (`short_blocks`, `inline_max_statements`, `empty_blocks`, `inline_brace_space`, `max_line_length`), spacing (`space_around_assign`, `space_around_operators`, `space_after_comma`, `space_inside_parens`), blank lines (`max_blank_lines`, `blank_lines_before_slide`, `blank_lines_after_includes`), sorting (`sort_properties`, `sort_includes`), `hex_case`, `final_newline`, `line_ending`, and `comment_gap`. Unknown options and invalid values produce warnings.
- Safety checks read the formatted code again to verify that its tokens are unchanged. If they differ, the file is left untouched and an error (`E4001`, `E4002`) is reported. BOMs and UTF-16 encoding are preserved.
- In the editor, typing `#fmt ` autocompletes option names and values. Formatting a document without settings asks whether to insert the defaults. The **`Format` page** in Settings lets users choose the current document’s `#fmt` options from lists; changing a value adds or updates the line, and reset removes it.

#### Accessibility Checker

- The editor’s **Accessibility** tab, `templide tool access`, and `--check --access` run the same checks.

  | Code | Level | Description |
  |---|---|---|
  | `A0001` | Error | An image, video, audio object, or chart has no `alt` text (use `alt = ""` for decorative content) |
  | `A0002` | Warning | Low contrast between text and background |
  | `A0003` | Warning | Text smaller than 12pt |
  | `A0004` | Warning | A slide has no title (hidden slides are excluded) |
  | `A0005` | Warning | Reading order (code order) differs from the top-to-bottom order on screen |
  | `A0006` | Information | A slide contains many animations |
  | `A0007` | Warning | A table has no header row or column |
  | `A0008` | Information | A textless shape, line, connector, freeform, or background blur has no `alt` text |

#### Editor

- **Template/master editing views**: Open a `template` or a `master` `case` as if it were a single slide, similar to Unity’s prefab editing view. Open it from the `Templates and Masters` list in the lower-left corner, by double-clicking an element created by a template, or from `Edit Layout` in the slide settings. Return using the top bar or Esc. Moving elements or changing properties updates the definition’s code. Parameters without defaults are rendered with their type’s default value (0 for `int`, an empty list, and so on) and appear dimmed in the properties panel. Parameters can also be added, removed, assigned defaults, or cleared in this view. Animations and slideshow mode are disabled while editing a definition.
- **`History` tab**: Changes made after opening the editor are saved as snapshots, and any snapshot can be restored. Changes made by an AI agent (MCP) are marked `AI`. History is cleared when the tab is closed.
- **Asset library**: The `Assets` tab in the lower panel shows images, videos, and audio from the current folder and open `.tasset` files. Click an item to insert it into a slide; files used by the document are marked.
- The `Problems` tab shows diagnostic codes such as `E2117` together with line numbers.
- Syntax highlighting now covers `#fmt name value`, `list<…>`, and `cell(…)`.

#### MCP (AI Agent)

- `get_accessibility` returns accessibility issues with line numbers, the code on each line, and suggested fixes (enable `info` to include informational findings). Agents can fix issues without reading the entire document.
- The `mcpGuide` documentation now includes chart, math, format, and quality topics.
- **Assets**: `add_asset` copies an image, video, or audio file from disk into a `.tasset` bundle and returns a `file("bundle.tasset/name")` expression for use in code. If no bundle is specified, the document’s default bundle is used. Identical content is not added twice; files with the same name but different content receive a suffix.
- **Slides and history**: `edit_slides` (add, duplicate, delete, and move slides) and `document_history` (undo, redo, list history, and restore a point in time) mirror the editor’s corresponding actions. History entries are marked `AI`, and users can undo them.
- **Documents and files**: `save_document`, `format_document` (returns the default settings line to insert when `#fmt` is missing), `set_target`, `open_document` (use `create` for missing files; the opened document becomes the one subsequent tools operate on), and `build`’s `targets` option (build multiple targets at once; `["*"]` means all targets).
- **`get_definition`**: Returns the parameters, definition location, source, and slides and lines that use a template or master.

#### Server (LSP) Requests

The editor/compiler server now supports more requests. This section only affects users building their own editor.

- New requests: `templide/assets`, `templide/accessibility`, `templide/format`, and `templide/fmtOptions`.
- `templide/deck` and `templide/edit` accept `definition: {kind, name, case?}` to expand a template or master case as a slide. `templide/schema` now includes `definitions`.
- New editing operations: `var_add`, `var_remove`, `var_default`, and `var_default_clear`.

### Changed

- **Reduced MCP token usage**: The guide now advises against routinely calling `get_editor_state` and `render_slide` (one image uses about 1.2k tokens). `edit_document` responses no longer include `renamedIds`, and errors and warnings are included only when present.
- Comments immediately above a slide move with the slide when slide order changes.
- Accessibility finding `A0004` now includes guidance on which layout to use and what to put in `title`.
- The README now documents charts, equations, exit codes, `--format`, the formatter, accessibility checks, and template/master editing views.
- The third-party license list generated by `tools/gen_licenses.py` now includes KaTeX (MIT) and KaTeX fonts (SIL OFL 1.1).
- `python build.py --fast`: Builds the editor with thin LTO (`release-thin` profile), reducing the final `templide-editor` step from about 82 seconds to 30 seconds. The executable is about 8% larger, and the installer name includes `-fast` so it does not overwrite release builds. Production builds (`python build.py`, CI) continue to use `release`.

### Performance (Development Environment)

- Fixed a startup pause of more than 10 seconds in `npm run tauri dev`, caused by the development server watching `src-tauri` (Rust build output with tens of thousands of files). Time to the first slide decreased from about 12.7 seconds to about 2.7 seconds (`editor/vite.config.ts`).
- The editor no longer waits for event listeners with `await` during startup, so the first slide appears about 35 ms sooner.

### Tests

- Added code formatter tests (`test/cases/fmt.txtar`) and diagnostic catalog checks (`test/catalog_check.py`). Cases for charts, equations, accessibility, template/master editing, and diagnostic output formats were added to the existing `.txtar` suite.

## [1.3.0] - 2026-10-15

Changes since v1.2.2. This release adds tables and PNG export, introduces `std/styles` to the standard library, and expands the information available to AI agents (MCP) and compiler tests.

### Breaking Changes

- **`bold`, `reset`, `brgap`, and `yellow` moved from `std/stddef` to `std/styles`.** Documents that use these names with only `#include <std/stddef>` now produce an unknown-name error. Add `#include <std/styles>`.

### Added

#### Language and Output

- **`table` object**: Define rows of cells such as `cells = [["Name", "Q1"], ["Seoul", "10"]];`. Configure header rows and columns (`header_rows`, `header_cols`), banding (`banded_rows`, `banded_cols`), column widths and row heights (`col_widths`, `row_heights`, including ratios), cell padding, and alignment. Use `cell(row, column) { col_span = 2; fill = hex(FFE699); }` to merge cells or style an individual cell (indices start at 1). PPTX output uses PowerPoint tables; HTML, PDF, and PNG render the same layout. See `examples/tables.tlide`.
- **`list<T>` type**: Declare list types such as `list<list<text>>` and write values as `[a, b]`. Lists can also be used in `object` and `template` `var` declarations.
- **`type = png` target**: Creates one PNG file per slide. With `path = "slides.png"`, the output files are `slides-1.png`, `slides-2.png`, and so on. Use `scale` (0.1–10) to adjust the scale; `transparent = true` makes the background transparent. Like PDF output, this requires Chrome or Edge, shows all objects without transitions or animations, and excludes hidden slides.
- **`std/styles`**: Provides basic text styles (`bold`, `italic`, `underline`, `strike`, `uppercase`, `superscript`, `align_*`, `reset`, `brgap(n)`, and others) and standard web/CSS color names (`red`, `navy`, `orange`, and others) through `#include <std/styles>`.

#### PowerPoint Import

- PowerPoint tables are imported as `table` objects. Previously, tables were displayed in gray and could not be selected. Header, banding, first-row, and first-column styles are expanded and applied to individual cells.

#### Editor

- **Table editing**: Select cells on the canvas, including ranges, and edit their text. Add or remove rows and columns, merge or split cells, and drag to change column widths and row heights. Configure cell formatting and per-side borders in the `Cells` section of the properties panel. The `Insert Table` toolbar button lets you choose the number of rows and columns.
- Paths in `#include "path"` now autocomplete one directory level at a time.

#### MCP (AI Agent)

- `get_selection` returns the code cursor, selected code, selected canvas element values, and selected cell ranges in a table.
- `get_language_guide` explains `.tlide` syntax by topic and obtains the compiler’s known objects, properties, enums, transitions, and animation lists.
- `get_resources` returns the names of files included with `#include` and files in `.tasset` bundles, including named images and media.
- `get_editor_state` now returns compact JSON without indentation and includes the list of open files.

#### Tests

- `python test/run.py`: Runs compiler snapshot tests and error/warning tests. Python 3.10 or later is required.

### Fixed

- Fixed an issue where a block immediately following an `#include` line (such as `slide { ... }`) was read as part of the include, causing incorrect autocomplete.

## [1.2.2] - 2026-10-15

Changes since v1.2.1. This release adds PDF export, alternative text (`alt`), support for files with multiple encodings, and `--check` and `--watch`.

### Added

#### Language and Output

- **`type = pdf` target**: Prints slides to PDF through headless Chrome or Edge. Each slide becomes one page; all objects are shown without transitions or animations, and hidden slides are excluded. If the browser is installed elsewhere, set the executable path with the `TEMPLIDE_BROWSER` environment variable.
- **`alt` property**: Adds alternative text to every object. In PPTX it becomes PowerPoint alternative text (`descr`); in HTML it becomes `aria-label` and `role`. PowerPoint import also maps alternative text to `alt`.

#### Command Line

- `--check`: Checks errors and warnings without writing files. The exit code is 1 when errors are present and 0 when there are warnings only. Documents without targets can also be checked, and PDF targets are checked without a browser.
- `--watch`: Rebuilds whenever the document or an included resource (`#include`, image, audio, or `.tasset` bundle) changes. Watching continues after errors, and `--watch` can be combined with `--check`.

#### Encoding

- **Compiler**: Reads `.tlide` files saved as UTF-8 (with or without a BOM) and UTF-16 with a BOM. Other encodings report an error with the line of the first invalid byte. Previously, BOM-prefixed files failed with `Unexpected token` at 1:1, while EUC-KR files passed without an error.
- **Editor**: Opens and saves files in multiple encodings, like VS Code. It checks for a BOM first and otherwise uses the default encoding from Settings (UTF-8 by default). Encoding auto-detection can be enabled in Settings. The encoding indicator at the top-right of the code panel provides `Reopen with Encoding` and `Save with Encoding`. Saving preserves the original encoding and BOM; if a character cannot be represented, saving is cancelled and the character is reported.

#### Editor

- Edit `alt` in the `Accessibility` section of the properties panel.

### Changed

- Running `templide` without a file now prints usage information. Previously, it opened a hard-coded path on the development machine.

### Fixed

- Fixed `Illegal byte sequence` failures when Korean filenames were passed as arguments on Windows. Arguments are now read as UTF-8.
- Fixed corrupted compiler messages when the Windows console uses CP949. The output code page is switched to UTF-8 while writing to the console and restored afterward.
- Invalid Korean characters are now reported as the complete character, such as `Unexpected token: '한'`, instead of only the first byte.

## [1.2.1] - 2026-10-09

Changes since v1.2.0. This release adds the Settings window and Korean/English UI languages.

### Added

- **Settings window**: Open it by clicking the logo. It includes `General` (UI language and whether to start with the code panel expanded), `Code Editor`, `Compiler`, `AI Agent`, `Slideshow`, `Shortcuts`, and `About` pages.
- **UI language**: Choose Korean or English. The editor UI changes, but compiler messages do not.
- **Compiler path**: Select the compiler executable in Settings and apply the change without restarting the editor. If the path cannot be executed, templide falls back to the default compiler and explains why. Priority is Settings, `TEMPLIDE_EXE`, the installed compiler, then the development build.
- **Custom shortcuts**: Assign and reset shortcuts for commands. Shortcuts for new file, open, close tab, next/previous tab, export, settings, bold/italic/underline, new slide, zoom, and other common actions are included.

### Changed

- Version information and open-source licenses were moved from the logo menu to the `About` page in Settings.

## [1.2.0] - 2026-10-08

Changes since v1.1.1. This release adds PowerPoint import and `.tasset` asset bundles, and the editor now opens multiple files in tabs.

### Breaking Changes

- **The `path` property of `image`, `video`, and `audio` objects is now `data`.** Values can be `file("path")`, `asset("name")`, a named `image` value, or a string path. Using `path` produces an error instructing you to use `data`.
- **The click action that opens a file is now `action_file("...")` instead of `file("...")`.** `file()` now represents an image, video, or audio file.

### Added

#### PowerPoint Import

- Right-click `.pptx`, `.pptm`, `.ppsx`, or `.potx` files in Windows Explorer and select `Import with templide` to open the import window. Each file displays slide masters (layouts and shapes) and slides (objects, animations, backgrounds, transitions, and notes) in a tree; only checked items are converted to `.tlide`. Images and media are placed in a `.tasset` bundle with the same name. Multiple files can be opened together or dragged into the window.
- Unsupported objects (charts, SmartArt, equations, OLE, ink, and others) are shown in gray and cannot be selected. If a selected slide’s layout is not selected, templide reports an error and blocks the import. Connectors attached to unselected objects are converted to lines, and slide links are renumbered (links to removed slides are removed and reported).

#### Asset Bundles

- **`.tasset`**: A bundle file in zip format that collects images, videos, and audio.
- **`asset "x.tasset" [by { ... }] [default];`**: Loads a bundle. Without `by`, all files are referenced by filename; `* as name` or `file as name` can assign names. `default` marks the bundle where images and media inserted by the editor are stored.
- **`image name = value;`, `video`, and `audio`**: Assign names to files. Values can be `file("a.png")` (a file on disk), `file("x.tasset/b.png")` (a file inside a bundle without a declaration), or `asset("name")` (a file from a loaded bundle). Ambiguous duplicate names are reported at the point of use.

#### Language

- **Color functions**: Use `hsl`, `hwb`, `lab`, `lch`, `oklab`, and `oklch` with CSS Color 4 units and ranges. Out-of-sRGB colors (`lab`, `lch`, `oklab`, `oklch`) are gamut-mapped to the nearest color by reducing chroma and produce a warning.
- **`font("name")`**: Reference a font by name.
- **Different masters per slide**: Previously, all slides had to use the same master.

#### Editor

- **File tabs**: Open multiple files in tabs and preserve each tab’s editing state (undo history, selection, and slide position). Closing a tab with unsaved changes prompts for confirmation. Files opened from Explorer use a tab in an existing window.
- **`.tasset` tabs**: View, add, rename, and delete images, videos, and audio in a bundle. When renaming or deleting, the editor shows where the file is used in open documents; renaming also updates the code at those locations.
- **Font picker**: Choose from fonts installed on the computer.
- **Color picker**: A color swatch appears before color functions in code and opens the picker when clicked. Values are written back using the original form, such as `hex`, `rgb`, `hsl`, or `oklch`.
- Images, videos, and audio inserted through the editor are placed in the default bundle. If no default bundle exists, the editor asks where to save it, creates the bundle, and inserts `asset "..." default;`.

#### Development

- `python tools/fill_constants.py --version X.Y.Z` updates the versions scattered across the editor’s npm package, Rust crate, and Tauri configuration in one command.

### Fixed

- Fixed `.tasset` code that failed to compile on other platforms.
- Set the macOS app’s minimum supported version to 11.0, the oldest version supported by Apple Silicon.
- `--ast` now outputs `asset`, `image`, `video`, and `audio` declarations.

## [1.1.1] - 2026-10-07

Changes since v1.1.0. This release adds Windows Explorer menus and the export window.

### Added

- **Explorer `New` menu**: Create a `templide slide` (`.tlide`). The new document contains one title text box and a `pptx` target.
- **Right-click `Export with templide` for `.tlide` files**: Opens only the export window without opening the editor. Choose one or more targets, build them together, and open the generated files or show them in their folder.
- **The editor’s Export button** now uses the same target-selection window. Previously, it built only the selected target.
- Added the `templide/targets` request to the compiler server so the export window can retrieve the target list efficiently.
