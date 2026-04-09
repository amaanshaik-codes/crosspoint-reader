# CrossPoint Reader - My Experimental Fork

This is my personal CrossPoint fork.

I use this firmware as an experimental branch where I can quickly test new ideas and features without being limited by the official project scope. My goal is simple: improve the real reading experience on the XTeink X4 and keep iterating fast.

## Credits

- Original CrossPoint project and core firmware work: [daveallie](https://github.com/daveallie)
- Official upstream repository: [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
- Dictionary feature source/inspiration: [kurtisgrant/crosspoint-reader](https://github.com/kurtisgrant/crosspoint-reader)

## What I Focus On In This Fork

- Experimental feature work
- EPUB reading quality and rendering improvements
- Dictionary lookup and reading-assistance tools
- UI/UX changes based on practical usage, not strict upstream scope

## Dictionary Setup

Dictionary lookup uses StarDict-style files from SD root.

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

The first lookup may be slower while index/cache data is initialized. Later lookups are much faster.

## Demo

<video src="./docs/media/Dict-feat.mp4" controls width="900"></video>

If your viewer does not render the player:

- [Open the demo video](docs/media/Dict-feat.mp4)

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

This fork is intentionally experimental and may diverge significantly from upstream as I test new functionality.
