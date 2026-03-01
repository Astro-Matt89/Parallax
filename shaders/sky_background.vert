#version 450

// -----------------------------------------------------------------
// Sky background vertex shader — fullscreen triangle
//
// Generates a single triangle covering the entire screen using
// gl_VertexIndex (0, 1, 2). No vertex buffer needed.
// -----------------------------------------------------------------

layout(location = 0) out vec2 v_uv;

void main()
{
    // Fullscreen triangle trick:
    //   Vertex 0: (-1, -1)  UV (0, 0)
    //   Vertex 1: ( 3, -1)  UV (2, 0)
    //   Vertex 2: (-1,  3)  UV (0, 2)
    // The rasterizer clips to the viewport, covering exactly the screen.
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}