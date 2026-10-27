# EnergyEngine
A small light-weight engine designed to make games with baked lighting
# Why was it made?
I like how performant and good Counter-Strike 2™ is, and I hate how hard it is to achieve such performance in any other engine

I also hate how bloated modern game engines are which makes them easy to be used but maked them hard to modify
# Current features
1. Multi-level support with effecient loading/unloading process ( Load new level and then Unload old level -> reduces loading times greatly due to how asset streaming is implemented )
2. Baked lighting
3. In-App Console
4. Logger
5. Crash handler
6. Fully customizable material shaders
# File formats
The engine uses custom file formats for assets, so here are all of them
| Implemented | Format | Full name | Description |
| ----------- | ------ | --------- | ----------- |
- [x] .eemf -> Energy Engine Mesh File            | obj, fbx, glb e.t.c.
- [ ] .eetf -> Energy Engine Texture File         | png, jpg, bmp e.t.c.
- [ ] .eevf -> Energy Engine Vector File          | svg
- [x] .eesf -> Energy Engine Shader File          | spv
- [ ] .eelf -> Energy Engine Level File           | game levels
- [ ] .eeaf -> Energy Engine Animation File       | any keyframed animation, not only for bones
- [x] .eerf -> Energy Engine Render material File | materials
- [ ] .eeif -> Energy Engine user Interface File  | graphical user interfaces
- [ ] .eeof -> Energy Engine AUdio File           | at least i pronounce that as "ow-dee-ow"
- [ ] .eexf -> Energy Engine particle efX File    | effects
