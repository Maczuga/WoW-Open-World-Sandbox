Based on: https://github.com/Maczuga/SunwellCore
Purpose: open world (no spawns related to World of Warcraft, pure sandbox).
Targets: lightweight, easy to develop, get rid of possible DBC files in favor or custom-written spells and such.

Player should be treated only as a viewer - most of UI actions (queueing/changing difficulties) won't work as it's not required by project.

What NOT to expect:
- achievements (functionality disabled/removed)
- groups (raid functionality disabled/removed, only regular groups)
- instnaced maps/scripts (functionality disabled/removed)
- battlegrounds/battlefields/arenas (see above) (functionality disabled/removed)
- dual spec (not needed by sandbox AI)
- setting raid difficulty
- banning (you don't want someone to access - remove account)
- leveling (levels are only are indicators for actors)

Also:
- Login cinematics are skipped
- There are no character/faction/class restricts
- Only map 0 and 1 are available. Possible limitation to only 1 of them in future.
- May be not working on other DBC files than enUS due to removed DBC locale system (as the plan is to avoid using DBC files at all costs)

TODO: 
- remove linked respawn (used only in instancable maps)
- remove who list (and implement nearby Actors command/listing)
- Playerable NPCs
- Name generation for units (first, 2nd* and last name)
- NPC types (internal, for housings, trees, enviroment details)
- Actor traits (for jobs and similar)