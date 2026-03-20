#ifndef GENERAL_H
#define GENERAL_H

struct Vector2 {
    float x;
    float y;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Vector4 {
    float x;
    float y;
    float z;
    float w;
};

struct Colour {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

struct Object {
    struct Vector3 chunk;
    struct Vector3 position;
    struct Vector3 direction;
    struct Vector3 size;
    struct Vector3 centerOfMass;
};

struct Object newObject();

#endif