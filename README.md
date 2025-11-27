Vulkan renderer working in progress, with mesh shading and ray tracing written in C23 with minimal library dependencies (cgltf, volk, stb image) written from scratch for Win32.

Orbit camera usage: left-click to orbit eye, right-click to pan, wheel to zoom in and out.

Ray tracing with queries with shadow rays:

<img width="1920" height="1032" alt="{E5C994CD-BCB1-4120-B505-E866BAE89B35}" src="https://github.com/user-attachments/assets/c044e22b-ec9f-4699-a52c-24005049d510" />

Colors highlighting individual meshlets:

<img width="1920" height="1032" alt="{2C9913E1-49C9-47D6-93AF-40BEE56DDC8F}" src="https://github.com/user-attachments/assets/bb3df0fa-2574-41c4-87b7-dbe37a2ebf48" />

Ray tracing with queries with shadow rays:

<img width="1920" height="1034" alt="{705D6244-9119-4BFB-8BC9-E618A5A0BF66}" src="https://github.com/user-attachments/assets/24b02378-aede-44cb-9cc2-c11bdef8dd64" />

Colors highlighting individual meshlets:

<img width="1920" height="1031" alt="{877D303F-569C-4ECF-9B6A-6330D98CCDF9}" src="https://github.com/user-attachments/assets/a98b7fbc-c724-4878-bb9d-702cab72a615" />

For command line build, follow these steps:

0. run 'git submodule update --init'

1. run 'find-cl.bat'
2. run 'shader_build.bat'
3. run 'code\build.bat r' for release build
4. run 'build\vulkan_3d_release.exe sponza/sponza.gltf'

For msvc build, open the project under win32-solution.

Tested on NVIDIA and AMD vendors.

TODO: Support for other platforms (linux, macos, ios)
