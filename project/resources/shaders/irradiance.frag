#version 450

// Convolves the environment cubemap into a diffuse irradiance cubemap by
// cosine-weighted hemisphere integration around each direction. Sampled in the
// lighting pass for the diffuse IBL ambient term.

layout(location = 0) in vec3 vLocalPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube uEnvironment;

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(vLocalPos);

    // Build a tangent basis around N.
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3 irradiance = vec3(0.0);
    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // Spherical -> tangent space -> world.
            vec3 tangentSample = vec3(sin(theta) * cos(phi),
                                      sin(theta) * sin(phi),
                                      cos(theta));
            vec3 sampleVec = tangentSample.x * right
                           + tangentSample.y * up
                           + tangentSample.z * N;

            irradiance += texture(uEnvironment, sampleVec).rgb
                        * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }

    irradiance = PI * irradiance / nrSamples;
    outColor = vec4(irradiance, 1.0);
}
