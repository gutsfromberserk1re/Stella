# Whisper model downloads

Stella's subtitle model manager offers three optional multilingual Whisper models:

| Tier | Model | Approximate size |
|---|---|---:|
| Performance | Whisper Base Q8_0 | 78 MiB |
| Balanced | Whisper Small Q8_0 | 252 MiB |
| Quality | Whisper Medium Q5_0 | 514 MiB |

No model weights are stored in the repository or copied into Lite/Full CI artifacts.
Models are downloaded on demand to the per-user `Stella/Models` directory:

- Windows: `%LOCALAPPDATA%/Stella/Models`
- macOS: `~/Library/Application Support/Stella/Models`
- Linux: `$XDG_DATA_HOME/Stella/Models`, falling back to `~/.local/share/Stella/Models`

Downloads use HTTPS from the pinned `ggerganov/whisper.cpp` Hugging Face revision
`0b364b566045a405be7225ee1e415a073e04da77`. Each completed file is SHA-256 checked
before its `.part` file is atomically renamed into place. Interrupted downloads keep
the partial file and resume with HTTP Range when the server supports it.

The backend download worker emits progress, verification, completion, cancellation,
and error events over Stella's existing submit/poll message channel.

## Upstream model attribution

The model weights are distributed by the `ggerganov/whisper.cpp` project. Whisper
models are based on OpenAI Whisper. See `WHISPER_MODEL_NOTICES.md` for upstream links
and the Whisper/whisper.cpp license references.
