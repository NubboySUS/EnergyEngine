# EnergyEngine
A small **light-weight** engine designed to make games **with baked lighting**
# Why was it made?
I like how performant and good [*Counter-Strike 2™*](https://store.steampowered.com/app/730/CounterStrike_2/) is, and I hate how *hard* it is to achieve such performance in *any other engine*

I also hate how *bloated* modern game engines are which makes them *easy to be used* but maked them *hard to modify*
# Current features
1. ```Multi-level support with effecient streaming process```
2. ```Baked lighting```
3. ```In-App Console```
4. ```Logger```
5. ```Crash handler```
6. ```Fully customizable material shaders```
7. ```Custom file formats```
# File formats
Speaking of file formats, the engine uses custom file formats for assets, so here are all of them:
| Implemented | Extension | Full name | Description |
| ----------- | --------- | --------- | ----------- |
| ✅ TRUE  | .eemf | **E**nergy **E**ngine **M**esh **F**ile            | Optimized `.obj`, `.fbx`, `.glb` e.t.c. with cool features. Currently only `.obj`
| ❌ FALSE | .eetf | **E**nergy **E**ngine **T**exture **F**ile         | Optimized `.png`, `.jpg`, `.bmp` e.t.c. with cool features
| ❌ FALSE | .eevf | **E**nergy **E**ngine **V**ector **F**ile          | Optimized `.svg` with cool features. Will likely be merged with `.eetf`
| ✅ TRUE  | .eesf | **E**nergy **E**ngine **S**hader **F**ile          | Same `.spv` but with header
| ❌ FALSE | .eelf | **E**nergy **E**ngine **L**evel **F**ile           | Game Levels / Maps
| ❌ FALSE | .eeaf | **E**nergy **E**ngine **A**nimation **F**ile       | Any animation, not just bones
| ✅ TRUE  | .eerf | **E**nergy **E**ngine **R**ender material **F**ile | Materials
| ❌ FALSE | .eeif | **E**nergy **E**ngine user **I**nterface **F**ile  | Graphical User Interfaces
| ❌ FALSE | .eeof | **E**nergy **E**ngine **AU**dio **F**ile           | Audio files
| ❌ FALSE | .eexf | **E**nergy **E**ngine particle e**fX** **F**ile    | Effects
# Plans for future
1. ```Add support for Real-Time lighting```
2. ```Add physics```
3. ```Replace ImGui with custom User Interface API```
4. ```Add Editor with Graphical User Interface```
5. ```Add Template project made that works without having to modify Engine/ folder and could be easily removed```
# Using this engine for your projects
This engine is licensed under [MIT license](https://github.com/NubboySUS/EnergyEngine/blob/main/LICENSE), which allows you to use this engine under the condition that you include this LICENSE in your project
