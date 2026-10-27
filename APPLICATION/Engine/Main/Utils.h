#pragma once
#include <array>
#include <string>
#include <sstream>
#include <vector>
#include <cmath>
#include <unordered_map>
#include <random>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define M_PI 3.1415926535897932384626433832795
#define FEPSILON 1e-6f
#define EPSILON 1e-12

constexpr double degtorad = M_PI / 180.0;
constexpr double radtodeg = 180.0 / M_PI;
constexpr float fdegtorad = static_cast<float>(M_PI) / 180.0f;
constexpr float fradtodeg = 180.0f / static_cast<float>(M_PI);

inline int randomInt(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distrib(min, max);
    return distrib(gen);
}
inline double randomDouble(double min, double max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<double> distrib(min, max);
    return distrib(gen);
}
inline float randomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distrib(min, max);
    return distrib(gen);
}

inline bool TrimInt(const std::string& text, int& output)
{
    try
    {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size())
            return false;
        output = value;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

inline static std::string Trim(std::string value) {
    value.erase(value.begin(),
        std::find_if(value.begin(), value.end(), [&](char c)
            {
                return std::isspace(static_cast<unsigned char>(c)) == 0;
            }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&](char c)
            {
                return std::isspace(static_cast<unsigned char>(c)) == 0;
            }).base(), value.end());
    return value;
}
inline static bool ParseCsvLine(std::string_view line, std::vector<std::string>& fields)
{
    fields.clear();
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;
                }
                else
                    quoted = false;
            }
            else
                current += c;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == ',') {
            fields.push_back(Trim(std::move(current)));
            current.clear();
            continue;
        }
        current += c;
    }
    if (quoted)
        return false;
    fields.push_back(Trim(std::move(current)));
    return true;
}
inline static std::string ToLower(std::string value) {
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
inline static std::vector<std::string> Tokenize(std::string_view text) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"')
                quoted = false;
            else if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\'))
                current += text[++i];
            else
                current += c;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                result.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (!current.empty())
        result.push_back(std::move(current));
    return result;
}
inline static std::vector<std::string> SplitCommands(std::string_view input) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '"' && (i == 0 || input[i - 1] != '\\'))
            quoted = !quoted;
        if (c == ';' && !quoted) {
            if (!current.empty()) {
                result.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (!current.empty())
        result.push_back(std::move(current));
    return result;
}
inline static void CopyToInputBuffer(std::array<char, 4096>& buffer, std::string_view text) {
    const size_t copyLength = (std::min)(text.size(), buffer.size() - 1);
    std::copy_n(text.data(), copyLength, buffer.data());
    buffer[copyLength] = '\0';
}
inline std::vector<float> ParseNumbers(const std::string& value) {
    std::vector<float> numbers;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        try { numbers.push_back(std::stof(Trim(token))); }
        catch (...) { numbers.push_back(0.0f); }
    }
    return numbers;
}
inline static double DegreesToRadians(double degrees) { return degrees * degtorad; }
inline static double RadiansToDegrees(double radians) { return radians * radtodeg; }
inline static float DegreesToRadians(float degrees) { return degrees * fdegtorad; }
inline static float RadiansToDegrees(float radians) { return radians * fradtodeg; }
inline static double WrapDegrees(double degrees) {
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0.0)
        wrapped += 360.0;
    return wrapped;
}
inline float FiniteOr(float value, float fallback = 0.0f) noexcept { return std::isfinite(value) ? value : fallback; }
inline double FiniteOr(double value, double fallback = 0.0) noexcept { return std::isfinite(value) ? value : fallback; }
inline bool NearlyEqual(float a, double b, double epsilon = 1e-4) { return std::isfinite(a) && std::isfinite(b) && std::abs(static_cast<double>(a) - b) <= epsilon; }

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    Vector3() = default;
    Vector3(double x, double y, double z) : x(x), y(y), z(z) {}

    // VECTOR AND VECTOR OPERATORS
    Vector3 operator+(const Vector3& other) const { return { x + other.x, y + other.y, z + other.z }; }
    Vector3 operator-(const Vector3& other) const { return { x - other.x, y - other.y, z - other.z }; }
    Vector3 operator*(const Vector3& other) const { return { x * other.x, y * other.y, z * other.z }; }
    Vector3 operator/(const Vector3& other) const { return { x / other.x, y / other.y, z / other.z }; }
    Vector3& operator+=(const Vector3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vector3& operator-=(const Vector3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vector3& operator*=(const Vector3& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
    Vector3& operator/=(const Vector3& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
    bool operator==(const Vector3& other) const { return { x == other.x && y == other.y && z == other.z }; }
    bool operator>=(const Vector3& other) const { return { x >= other.x && y >= other.y && z >= other.z }; }
    bool operator<=(const Vector3& other) const { return { x <= other.x && y <= other.y && z <= other.z }; }
    bool operator!=(const Vector3& other) const { return { x != other.x && y != other.y && z != other.z }; }
    bool operator>(const Vector3& other) const { return { x > other.x && y > other.y && z > other.z }; }
    bool operator<(const Vector3& other) const { return { x < other.x && y < other.y && z < other.z }; }

    // VECTOR AND NUMBER OPERATORS
    Vector3 operator+(double scalar) const { return { x + scalar, y + scalar, z + scalar }; }
    Vector3 operator-(double scalar) const { return { x - scalar, y - scalar, z - scalar }; }
    Vector3 operator*(double scalar) const { return { x * scalar, y * scalar, z * scalar }; }
    Vector3 operator/(double scalar) const { return { x / scalar, y / scalar, z / scalar }; }
    Vector3& operator+=(double scalar) { x += scalar; y += scalar; z += scalar; return *this; }
    Vector3& operator-=(double scalar) { x -= scalar; y -= scalar; z -= scalar; return *this; }
    Vector3& operator*=(double scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vector3& operator/=(double scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

    // CREATE VECTORS
    static Vector3 Zero() { return { 0.0, 0.0, 0.0 }; }
    static Vector3 One() { return { 1.0, 1.0, 1.0 }; }
    static Vector3 XAxis() { return { 1.0, 0.0, 0.0 }; }
    static Vector3 YAxis() { return { 0.0, 1.0, 0.0 }; }
    static Vector3 ZAxis() { return { 0.0, 0.0, 1.0 }; }
    // GET VECTOR LENGTH
    double Magnitude() const {
        return std::sqrt(x * x + y * y + z * z);
    }
    // GET VECTOR WITH LENGTH ONE OR ZERO
    Vector3 Unit() const {
        double mag = Magnitude();
        if (mag > 0.0) {
            return { x / mag, y / mag, z / mag };
        }
        return Zero();
    }
    // GET VECTOR WITH ALL AXES >= 0
    Vector3 Abs() const {
        return { std::abs(x), std::abs(y), std::abs(z) };
    }
    // ROUND ALL VECTOR AXES TO NEAREST WHOLE NUMBER >= TO THEM
    Vector3 Ceil() const {
        return { std::ceil(x), std::ceil(y), std::ceil(z) };
    }
    // ROUND ALL VECTOR AXES TO NEAREST WHOLE NUMBER <= TO THEM
    Vector3 Floor() const {
        return { std::floor(x), std::floor(y), std::floor(z) };
    }
    // GET VECTOR AXES SIGN: AXIS < 0 -> -1; AXIS = 0 -> 0; AXIS > 0 -> 1;
    Vector3 Sign() const {
        return { x == 0.0 ? 0.0 : (x < 0.0 ? -1.0 : 1.0), y == 0.0 ? 0.0 : (y < 0.0 ? -1.0 : 1.0), z == 0.0 ? 0.0 : (z < 0.0 ? -1.0 : 1.0) };
    }
    // GET VECTOR CROSS PRODUCT
    Vector3 Cross(const Vector3& other) const {
        return { y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x };
    }
    // GET ANGLE BETWEEN TWO VECTORS
    double Angle(const Vector3& other, const Vector3& axis) const {
        Vector3 crossProd = Cross(other);
        double sinAngle = crossProd.Dot(axis);
        double cosAngle = Dot(other);
        return std::atan2(sinAngle, cosAngle);
    }
    // GET VECTOR DOT PRODUCT
    double Dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    // LERP TWO VECTORS
    Vector3 Lerp(const Vector3& end, double t) const {
        return { x + t * (end.x - x), y + t * (end.y - y), z + t * (end.z - z) };
    }
    // ARE VECTORS EQUAL UP TO SOME PRECISION: CHECKS AXES AND NOT DISTANCE
    bool NearlyEqual(const Vector3& other, double epsilon) const {
        return (std::abs(x - other.x) <= epsilon) && (std::abs(y - other.y) <= epsilon) && (std::abs(z - other.z) <= epsilon);
    }
    // ARE VECTORS EQUAL UP TO SOME PRECISION: CHECKS DISTANCE AND NOT AXES
    bool DistNearlyEqual(const Vector3& other, double epsilon) const {
        return (*this - other).Magnitude() <= epsilon;
    }
    // GET VECTOR WITH ALL AXES SET TO A MAXIMUM BETWEEN TWO VECTORS
    Vector3 Max(const Vector3& vector) const {
        return { (std::max)(x,vector.x), (std::max)(y,vector.y), (std::max)(z,vector.z) };
    }
    // GET VECTOR WITH ALL AXES SET TO A MINIMUM BETWEEN TWO VECTORS
    Vector3 Min(const Vector3& vector) const {
        return { (std::min)(x,vector.x), (std::min)(y,vector.y), (std::min)(z,vector.z) };
    }
    // GET VECTOR WHERE AXES THAT ARE NOT FINITE BEING REPLACED WITH FALLBACK
    Vector3 FiniteOr(const Vector3& fallback = Vector3::Zero()) noexcept {
        return Vector3(std::isfinite(x) ? x : fallback.x, std::isfinite(y) ? y : fallback.y, std::isfinite(z) ? z : fallback.z);
    }
};
struct Vector2 {
    double x = 0.0;
    double y = 0.0;
    Vector2() = default;
    Vector2(double x, double y) : x(x), y(y) {}

    // VECTOR AND VECTOR OPERATORS
    Vector2 operator+(const Vector2& other) const { return { x + other.x, y + other.y }; }
    Vector2 operator-(const Vector2& other) const { return { x - other.x, y - other.y }; }
    Vector2 operator*(const Vector2& other) const { return { x * other.x, y * other.y }; }
    Vector2 operator/(const Vector2& other) const { return { x / other.x, y / other.y }; }
    Vector2 operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
    Vector2 operator-=(const Vector2& other) { x -= other.x; y -= other.y; return *this; }
    Vector2 operator*=(const Vector2& other) { x *= other.x; y *= other.y; return *this; }
    Vector2 operator/=(const Vector2& other) { x /= other.x; y /= other.y; return *this; }
    bool operator==(const Vector2& other) const { return { x == other.x && y == other.y }; }
    bool operator>=(const Vector2& other) const { return { x >= other.x && y >= other.y }; }
    bool operator<=(const Vector2& other) const { return { x <= other.x && y <= other.y }; }
    bool operator!=(const Vector2& other) const { return { x != other.x && y != other.y }; }
    bool operator>(const Vector2& other) const { return { x > other.x && y > other.y }; }
    bool operator<(const Vector2& other) const { return { x < other.x && y < other.y }; }

    // VECTOR AND NUMBER OPERATORS
    Vector2 operator+(double scalar) const { return { x + scalar, y + scalar }; }
    Vector2 operator-(double scalar) const { return { x - scalar, y - scalar }; }
    Vector2 operator*(double scalar) const { return { x * scalar, y * scalar }; }
    Vector2 operator/(double scalar) const { return { x / scalar, y / scalar }; }
    Vector2 operator+=(double scalar) { x += scalar; y += scalar; return *this; }
    Vector2 operator-=(double scalar) { x -= scalar; y -= scalar; return *this; }
    Vector2 operator*=(double scalar) { x *= scalar; y *= scalar; return *this; }
    Vector2 operator/=(double scalar) { x /= scalar; y /= scalar; return *this; }

    // CREATE VECTORS
    static Vector2 Zero() { return { 0.0, 0.0 }; }
    static Vector2 One() { return { 1.0, 1.0 }; }
    static Vector2 XAxis() { return { 1.0, 0.0 }; }
    static Vector2 YAxis() { return { 0.0, 1.0 }; }

    // GET VECTOR LENGTH
    double Magnitude() const {
        return std::sqrt(x * x + y * y);
    }
    // GET VECTOR WITH LENGTH ONE OR ZERO
    Vector2 Unit() const {
        double mag = Magnitude();
        return (mag > 0.0) ? (*this / mag) : Zero();
    }
    // GET VECTOR WITH ALL AXES >= 0
    Vector2 Abs() const {
        return { std::abs(x), std::abs(y) };
    }
    // ROUND ALL VECTOR AXES TO NEAREST WHOLE NUMBER >= TO THEM
    Vector2 Ceil() const {
        return { std::ceil(x), std::ceil(y) };
    }
    // ROUND ALL VECTOR AXES TO NEAREST WHOLE NUMBER <= TO THEM
    Vector2 Floor() const {
        return { std::floor(x), std::floor(y) };
    }
    // GET VECTOR AXES SIGN: AXIS < 0 -> -1; AXIS = 0 -> 0; AXIS > 0 -> 1;
    Vector2 Sign() const {
        return { x == 0.0 ? 0.0 : (x < 0.0 ? -1.0 : 1.0), y == 0.0 ? 0.0 : (y < 0.0 ? -1.0 : 1.0) };
    }
    // GET VECTOR CROSS PRODUCT
    double Cross(const Vector2& other) const {
        return x * other.y - y * other.x;
    }
    // GET ANGLE BETWEEN TWO VECTORS
    double Angle(const Vector2& other) const {
        return std::atan2(Cross(other), Dot(other));
    }
    // GET VECTOR DOT PRODUCT
    double Dot(const Vector2& other) const {
        return x * other.x + y * other.y;
    }
    // LERP TWO VECTORS
    Vector2 Lerp(const Vector2& end, double t) const {
        return { x + t * (end.x - x), y + t * (end.y - y) };
    }
    // ARE VECTORS EQUAL UP TO SOME PRECISION: CHECKS AXES AND NOT DISTANCE
    bool NearlyEqual(const Vector2& other, double epsilon) const {
        return (std::abs(x - other.x) <= epsilon) &&
            (std::abs(y - other.y) <= epsilon);
    }
    // ARE VECTORS EQUAL UP TO SOME PRECISION: CHECKS DISTANCE AND NOT AXES
    bool DistNearlyEqual(const Vector2& other, double epsilon) const {
        return (*this - other).Magnitude() <= epsilon;
    }
    // GET VECTOR WITH ALL AXES SET TO A MAXIMUM BETWEEN TWO VECTORS
    Vector2 Max(const Vector2& vector) const {
        return { (std::max)(x, vector.x), (std::max)(y, vector.y) };
    }
    // GET VECTOR WITH ALL AXES SET TO A MINIMUM BETWEEN TWO VECTORS
    Vector2 Min(const Vector2& vector) const {
        return { (std::min)(x, vector.x), (std::min)(y, vector.y) };
    }
};
struct Matrix4
{
    std::array<std::array<double, 4>, 4> m{};
    constexpr Matrix4() noexcept = default;
    static Matrix4 Identity()
    {
        Matrix4 result;
        result[0][0] = 1.0;
        result[1][1] = 1.0;
        result[2][2] = 1.0;
        result[3][3] = 1.0;
        return result;
    }
    [[nodiscard]] std::array<double, 4>& operator[](std::size_t row) noexcept
    {
        return m[row];
    }
    [[nodiscard]] const std::array<double, 4>& operator[](std::size_t row) const noexcept
    {
        return m[row];
    }
    Vector3 TransformPoint(const Vector3& point) const noexcept
    {
        const double x = m[0][0] * point.x + m[0][1] * point.y + m[0][2] * point.z + m[0][3];
        const double y = m[1][0] * point.x + m[1][1] * point.y + m[1][2] * point.z + m[1][3];
        const double z = m[2][0] * point.x + m[2][1] * point.y + m[2][2] * point.z + m[2][3];
        const double w = m[3][0] * point.x + m[3][1] * point.y + m[3][2] * point.z + m[3][3];
        if (std::abs(w) <= 1e-12 || !std::isfinite(w))
            return { x, y, z };
        const double inverseW = 1.0 / w;
        return { x * inverseW, y * inverseW, z * inverseW };
    }
    Vector3 TransformVector(const Vector3& vector) const noexcept
    {
        return {
            m[0][0] * vector.x + m[0][1] * vector.y + m[0][2] * vector.z,
            m[1][0] * vector.x + m[1][1] * vector.y + m[1][2] * vector.z,
            m[2][0] * vector.x + m[2][1] * vector.y + m[2][2] * vector.z,
        };
    }
    Matrix4 operator*(const Matrix4& rhs) noexcept
    {
        Matrix4 result;
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                for (std::size_t k = 0; k < 4; ++k)
                    result[row][column] += (*this)[row][k] * rhs[k][column];
            }
        }
        return result;
    }
};
struct Quaternion
{
    double x{ 0.0 };
    double y{ 0.0 };
    double z{ 0.0 };
    double w{ 1.0 };
    static Quaternion Identity() noexcept { return {}; };
    static Quaternion FromAxisAngle(const Vector3& axis, double radians) noexcept {
        const double lenSquare = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (lenSquare <= EPSILON)
            return Identity();
        const double scale = std::sin(radians * 0.5) / std::sqrt(lenSquare);
        return { axis.x * scale, axis.y * scale, axis.z * scale, std::cos(radians * 0.5) };
    }
    Quaternion Normalized() const noexcept {
        const double lenSquare = x * x + y * y + z * z + w * w;
        if (lenSquare <= EPSILON)
            return Identity();
        const double inverseLength = 1.0 / std::sqrt(lenSquare);
        return { x * inverseLength, y * inverseLength, z * inverseLength, w * inverseLength };
    }
    Quaternion Conjugate() const noexcept { return { -x, -y, -z, w }; }
    Vector3 Rotate(const Vector3& value) const noexcept {
        const Quaternion q = Normalized();
        const Quaternion rotated = q * Quaternion{ value.x, value.y, value.z, 0.0 } * q.Conjugate();
        return { rotated.x, rotated.y, rotated.z };
    }
    Quaternion operator*(const Quaternion& rhs) const noexcept {
        return { w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y, w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x, w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w, w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z };
    }
};