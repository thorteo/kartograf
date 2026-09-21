# Architecture Decision Records

Each record captures one decision, the context that forced it, and the options that
were rejected. The point is that a future reader — human or model — can tell whether a
decision still holds, instead of guessing why the code looks the way it does.

## Process

- New record: copy `template.md`, take the next number, set status `Proposed`.
- Once acted on, set status `Accepted`.
- **Never rewrite an accepted record to change its decision.** Write a new record that
  supersedes it, and edit the old one's status to `Superseded by NNNN`.
- Record the options you rejected and why. That is usually the most valuable part.

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-stack-selection.md) | Language, UI and rendering stack | Accepted |
| [0002](0002-no-game-process-interaction.md) | Never interact with the game process | Accepted |
| [0003](0003-overlay-window.md) | Overlay via DirectComposition, not a DirectX hook | Accepted |
| [0004](0004-threading-model.md) | Single guarded state, snapshot to renderer | Accepted |
