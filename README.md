Vulkan renderer with mesh shading and ray tracing written in C23 with minimal library dependencies (cgltf, volk, stb image) written from scratch.

Orbit camera usage: left-click to orbit eye, right-click to pan, wheel to zoom in and out.

<img width="1920" height="1032" alt="{E5C994CD-BCB1-4120-B505-E866BAE89B35}" src="https://github.com/user-attachments/assets/c044e22b-ec9f-4699-a52c-24005049d510" />

<img width="1920" height="1032" alt="{2C9913E1-49C9-47D6-93AF-40BEE56DDC8F}" src="https://github.com/user-attachments/assets/bb3df0fa-2574-41c4-87b7-dbe37a2ebf48" />

<img width="1920" height="1030" alt="{FAB0CEDE-C01B-4EBE-9374-22AA33A48A32}" src="https://github.com/user-attachments/assets/2e56047d-2156-4499-8adf-99b99670aef4" />

<img width="1919" height="1030" alt="image" src="https://github.com/user-attachments/assets/5092893f-1abe-4755-a838-b97330c3e65d" />

For command line build, follow these steps:

0. run 'git submodule update --init'

1. run 'find-cl.bat'
2. run 'shader_build.bat'
3. run 'code\build.bat r' for release build
4. run 'build\vulkan_3d_release.exe sponza/sponza.gltf'

For msvc build, open the project under win32-solution.

Tested on NVIDIA and AMD vendors.
