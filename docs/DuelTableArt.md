# Duel table presentation

The two-player table uses the Reborn stone and metal materials, two opposing
name plaques, the original left control wheel, right turn timer, and hand rack.
Names, roles, clan flags, and wind indicators are rendered by the game.
The gold seat wind marks the current turn; the centre of the left wheel shows
the hand wind. The FFA and Coalition board artwork stays as before.

The discard and claim controls sit to the right of the shared history. Up to
60 discards use 40 x 48 tiles; longer histories use the existing 24 x 30 tiles,
in 18 columns, keeping the full wall's history between the two plaques.
Scry Runes uses a separate row above the opposing name.

All four rule guardians remain visible between actions, as on the FFA table.
Their resting poses and animation frames share the same Duel position and
scale: Chao and Pung above the side controls, Game at the lower left, and Kong
beside the current discard. These are rule guardians, independent of player
seats. The screen scales its own frame copies without changing cached FFA art.

## Assets and provenance

- `themes/reborn/assets/images/table-duel.png`: production background, 1448 x
  1086, fitted to the logical 1024 x 768 table when it is opened. Generated on
  2026-09-06 with the built-in `image_gen` tool in image-edit mode, using the
  existing Reborn board `art/reborn/source/mahjong/board/screen3.png` as reference.
- `themes/reborn/assets/images/wind-glyphs.png`: unchanged copy of the existing
  transparent Reborn source sheet (1254 x 1254). Runtime crops exclude stray
  pixels at the lower sheet edges. Staged smooth reduction retains the alpha
  channel, avoiding the old atlas's opaque pink antialiasing fringe. No new wind
  artwork was generated.

The theme JSON selects the background only for Duel. Classic retains its own
artwork and uses two small framed name labels. Table wind improvements also
apply to Reborn FFA and Coalition.

## Background generation prompt

Use case: precise-object-edit. Asset type: production game UI background, 4:3 landscape, same dimensions/aspect as the supplied 1024x768 image. Input image 1 is the edit target: the existing dark fantasy rune game board. Adapt it for a TWO PLAYER DUEL while closely preserving the existing art direction: intricate weathered silver and muted brass, obsidian/slate stone with restrained deep turquoise seams, carved medieval metalwork, subdued realistic painterly game art, crisp fine edges.
Keep these key interface anchors in their exact relative positions: the small circular left control wheel centered at (192,334) with radius about 90, the small circular right timer centered at (832,334) with radius about 88, and the long ornate hand rack along the bottom at y=676..767. Keep the central metallic wind/spiral medallion near (512,334) but make it much more subtle, recessed into the dark stone.
Replace the FOUR large vacant circular corner seats and the strong diagonal X spokes with a composition clearly oriented from TOP to BOTTOM for two opposing players. Remove all four large corner seat sockets entirely, continue convincing dark slate/metal scenery there. Create two elegant shallow horizontal name plaques centered at (512,85) and (512,590), each spanning roughly x=315..710, height=50, empty dark interiors with fine silver/brass framing. They should be integrated into the board, not floating modern cards. The playable middle region x=300..725, y=135..545 must remain dark, uncluttered and low contrast for rune tiles, with a restrained ornamental border rather than a flat colored rectangle. Add subtle vertical engraved channels to imply the connection between the two seats. Preserve legibility: no bright glowing decorations, no textures resembling rune tiles in the playing area. All other space should retain rich matching dark stone texture and restrained filigree. EXACTLY TWO PLAYER PLAQUES. No portraits, no characters, no flags, no runes, no lettering, no text, no UI icons or numbers, no watermark. The result must feel like the same game and materials as the reference, adapted for two players.

The generated name plaques were at y=141 and y=535 after fitting; the interface
uses those observed positions rather than the originally requested positions.

## Validation

`local_modes_ui` exercises the real SDL screen. Set `FOUR_WINDS_UI_TEST_THEME`
to `reborn` or `classic`, `FOUR_WINDS_UI_TEST_LANGUAGE` to `ru` or `en`, and
`FOUR_WINDS_UI_SNAPSHOT_DIR` to an isolated directory to inspect the table at
24, 60, 61, and 136 discards, with revealed hand, melds and claim controls.
The fixtures restore the original match before running remaining regressions.
Continue also checks preservation of the visible action log's owner.

Manual phone checks for touch targeting, background/resume and Continue remain
part of release acceptance.
