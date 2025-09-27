#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) out vec4 out_color;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world_frag_pos;
layout(location = 2) in vec2 in_uv;
layout(location = 3) flat in uint in_textureID;

layout(set = 1, binding = 0)
uniform sampler2D textures[];

#if 0
float ndc_to_linear_z(float ndc_z, float near, float far)
{
   float u = far * near;
   float l = (near - far)*ndc_z + far;

   float linear_z = u / l;

   return linear_z;
}

vec3 hsv_to_rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 0.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
#endif

float G1(float NdotX, float k)
{
   return NdotX / (NdotX * (1.0 - k) + k);
}

#if 0
// Utility to unpack normal from normal map
vec3 getNormalFromMap(vec2 uv, vec3 normal, vec3 tangent, vec3 bitangent)
{
    vec3 tangentNormal = texture(normal, uv).xyz * 2.0 - 1.0;

    mat3 TBN = mat3(tangent, bitangent, normal);
    return normalize(TBN * tangentNormal);
}
#endif

void main()
{
#if 1
    // TODO: just for testing!!!
    vec3 albedo = pow(texture(textures[0], in_uv).rgb, vec3(2.2)); // sRGB to linear
    vec3 mr      = texture(textures[1], in_uv).rgb;
    float roughness = mr.g;
    float metallic  = mr.b;
    vec3 emissive   = texture(textures[2], in_uv).rgb;  // apply factor if needed
    float ao        = texture(textures[3], in_uv).r;
    vec3 normal     = texture(textures[4], in_uv).rgb;

    vec3 cameraPos = vec3(0.0, 0.0, 5.0);      // camera looking down -Z
    vec3 lightPos  = vec3(0.0, 1.0, 1.0);      // point light
    vec3 lightColor = vec3(1.0, 1.0, 1.0);     // white

    // --- Normals ---
    vec3 N = normalize(normal); // if you have tangents, use getNormalFromMap
    vec3 V = normalize(cameraPos - in_world_frag_pos);
    vec3 L = normalize(lightPos - in_world_frag_pos);
    vec3 H = normalize(V + L);

    // --- PBR Lighting ---
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Fresnel-Schlick
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);

    // Normal Distribution (GGX)
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    float D = a2 / (3.141592 * denom * denom);

    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    float G = G1(NdotV, k) * G1(NdotL, k);

    // Cook-Torrance BRDF
    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    // Final lighting
    vec3 diffuse = kD * albedo / 3.141592;
    vec3 radiance = lightColor;

    vec3 color = (diffuse + specular) * radiance * NdotL;
    color = color * ao; // apply AO
    color += emissive;  // add emissive

    // gamma correction back to sRGB
    out_color = vec4(pow(color, vec3(1.0/2.2)), 1.0);
    #else

    out_color = vec4(1.f, 1.f, 1.f, 1.f);
    #endif
}
