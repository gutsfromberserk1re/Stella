# Whisper model notices

Stella downloads pre-converted GGML model weights from:

- https://huggingface.co/ggerganov/whisper.cpp
- Pinned revision: https://huggingface.co/ggerganov/whisper.cpp/tree/0b364b566045a405be7225ee1e415a073e04da77

The upstream repository describes these as OpenAI Whisper models converted to GGML
format for use with whisper.cpp. The upstream model repository identifies its license
as MIT. The whisper.cpp implementation is also MIT licensed. Consult the upstream
repositories for the full license texts and any model-specific attribution updates:

- https://github.com/openai/whisper
- https://github.com/ggml-org/whisper.cpp

Stella does not bundle model weights. The user explicitly chooses which model to
retrieve, and model bytes are stored in the user's application-data directory.
