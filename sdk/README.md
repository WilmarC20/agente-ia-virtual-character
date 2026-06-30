# agenteIA SDK

Contracts and examples for themes, plugins, and third-party devices.

## Contracts

JSON Schemas in `sdk/contracts/`:

| Schema | Purpose |
|--------|---------|
| `event_envelope.schema.json` | Event Bus message wrapper |
| `intent_packet.schema.json` | STT → intent |
| `emotion_state.schema.json` | Emotion for UI/face |
| `actuation_plan.schema.json` | Brain → device actuation |
| `scene_command.schema.json` | Scene change |
| `theme_manifest.schema.json` | Theme package metadata |
| `widget_spec.schema.json` | Widget declaration |
| `layout_spec.schema.json` | Layout regions and hit zones |

## Examples

See `examples/` for minimal theme and plugin templates.
