# Lite Ray

This is my sandbox testing enviroment for voxel-based raytracing

<div align="left">
  <img src="res/nicer_lantern.png" width="128" alt="Description">
</div>

## Technical Goals

- Path tracing
    - using bitmasking layers for the grid-aligned voxels
    - using Octrees for dynamic voxels

- Lighting
    - direct lighting (shadow rays for performance)
    - global illumination
    - specular reflections
    - refraction

- Chunk loading
    - have around 1 GB of voxel data on the GPU at a time
    - stream chunks in out of this as the play moves around

- Basic terrain generation
    - just stacked noise maps really

- models
    - add voxel models to the world

- Networked
    - players stream chunks and other player positions over UDP
    - client side prediction

## Performace Goals

- can run at 100 fps using 720p on my RTX 3050 laptop
- i know this is possible because i can do this with Lay of the Land with RT enabled

## Why

eventually I want to develop a PVP and exploration based SMP game but im writing my own framework in C so this idea is totally unrealistic and will take years