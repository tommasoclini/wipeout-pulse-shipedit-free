# Changelog

All notable changes to this project will be documented in this file.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## UNRELEASED

### Changed
- Font textures are uploaded using power-of-two for OpenGL drivers without NPOT support


## 1.0.2 - 2022-01-03

### Changed
- Implemented native UI dialogs on macOS

### Fixed
- Triakis ship texture mapping error (Fixes #4, thanks to StingerTheRaven for reporting)


## 1.0.1 - 2021-07-06

### Added
- Command-line parsing and batch export mode (run `shipedit --help` for usage) (Fixes #3)

### Changed
- The folder picker for "Export" now remembers the last used folder (Fixes #1)
- Maximum number of save game slots is now 40 (was 20) (Fixes #2)

### Fixed
- Exporting of Mirage team skins has been fixed, thanks to Kreic69 for reporting


## 1.0.0 -- 2021-03-24

- Initial public release
