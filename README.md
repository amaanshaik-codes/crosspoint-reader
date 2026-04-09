# Amaan's Crosspoint Reader

This is my personal CrossPoint fork.

I use this firmware as an experimental branch where I can quickly test new ideas and features without being limited by the official project scope.

## Credits

- Shoutout to Dave Allie for making this firmware: [daveallie](https://github.com/daveallie)
- Official Crosspoint repository: [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
- Dictionary feature source: [kurtisgrant/crosspoint-reader](https://github.com/kurtisgrant/crosspoint-reader)

## What I Focus On In This Fork

- Experimental feature work
- EPUB reading quality and rendering improvements
- Dictionary lookup and reading-assistance tools
- UI/UX changes based on practical usage

## Dictionary Setup

Dictionary lookup uses StarDict-style files from SD root. The first lookup may be slower while index/cache data is initialized. Later lookups are much faster.

Included in this repo:

- [eng-dictionary/dictionary.dict](eng-dictionary/dictionary.dict)
- [eng-dictionary/dictionary.idx](eng-dictionary/dictionary.idx)
- [eng-dictionary/dictionary.ifo](eng-dictionary/dictionary.ifo)

Copy these to SD root:

- `dictionary.dict`
- `dictionary.idx`

Example SD layout:

```text
/
├── dictionary.dict
├── dictionary.idx
└── books...
```

## Build

```sh
git clone --recursive https://github.com/amaanshaik-codes/crosspoint-reader.git
cd crosspoint-reader
pio run -e default
```

## Flash

```sh
pio run --target upload
```

## Note

This fork is intentionally experimental and may diverge significantly from the main crosspoint reader.
