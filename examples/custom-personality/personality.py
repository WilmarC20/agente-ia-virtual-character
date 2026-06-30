"""Example custom personality — register in server_config.PERSONALITIES."""

PERSONALITY = {
    "id": "friendly",
    "presentation": "bender",
    "system_prompt_suffix": "Sos amable y breve. Máximo dos oraciones.",
    "behavior": {
        "microexp_rate": 0.8,
        "emotion_recovery_ms": 6000,
        "expressivity": 0.7,
    },
}
