# Rime input data and notices

The bundled data and schemas retain their upstream Rime notices. They are
imported through adam-ikari/term-ime and term-ime-dict; the latter is pinned in
`dependencies.json`. The MIT notices from those integration repositories do
not replace the original dictionary/schema licenses below.

- [rime-luna-pinyin](https://github.com/rime/rime-luna-pinyin/blob/master/LICENSE): LGPL-3.0; license blob 65c5ca88a67c30becee01c5a8816d964b03862f9.
- [rime-essay](https://github.com/rime/rime-essay/blob/master/LICENSE): LGPL-3.0; license blob 65c5ca88a67c30becee01c5a8816d964b03862f9.
- [rime-prelude](https://github.com/rime/rime-prelude/blob/master/LICENSE): LGPL-3.0; license blob 65c5ca88a67c30becee01c5a8816d964b03862f9.

Source YAML, dictionary and essay text are included in `terminal/assets/rime-data/`.
`tools/build_ime_data.py` and `tools/ime_deploy.cpp` produce the binary lookup
files from that source. OpenCC data uses the included OpenCC Apache-2.0 notice;
additional source acknowledgements remain in `luna_pinyin.dict.yaml`.
The runtime package includes the librime, YAML, LevelDB, Marisa and OpenCC
library license texts separately.
