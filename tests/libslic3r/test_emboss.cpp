#include <catch2/catch.hpp>

#include <libslic3r/Emboss.hpp>
#include <libslic3r/SVG.hpp> // only debug visualization

#include <optional>
#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/Utils.hpp> // for next_highest_power_of_2()

using namespace Slic3r;

namespace Private{
        
// calculate multiplication of ray dir to intersect - inspired by
// segment_segment_intersection when ray dir is normalized retur distance from
// ray point to intersection No value mean no intersection
std::optional<double> ray_segment_intersection(const Vec2d &r_point,
                                               const Vec2d &r_dir,
                                               const Vec2d &s0,
                                               const Vec2d &s1)
{
    auto denominate = [](const Vec2d &v0, const Vec2d &v1) -> double {
        return v0.x() * v1.y() - v1.x() * v0.y();
    };

    Vec2d  segment_dir = s1 - s0;
    double d           = denominate(segment_dir, r_dir);
    if (std::abs(d) < std::numeric_limits<double>::epsilon())
        // Line and ray are collinear.
        return {};

    Vec2d  s12         = s0 - r_point;
    double s_number    = denominate(r_dir, s12);
    bool   change_sign = false;
    if (d < 0.) {
        change_sign = true;
        d           = -d;
        s_number    = -s_number;
    }

    if (s_number < 0. || s_number > d)
        // Intersection outside of segment.
        return {};

    double r_number = denominate(segment_dir, s12);
    if (change_sign) r_number = -r_number;

    if (r_number < 0.)
        // Intersection before ray start.
        return {};

    return r_number / d;
}

Vec2d get_intersection(const Vec2d &               point,
                       const Vec2d &               dir,
                       const std::array<Vec2d, 3> &triangle)
{
    std::optional<double> t;
    for (size_t i = 0; i < 3; ++i) {
        size_t i2 = i + 1;
        if (i2 == 3) i2 = 0;
        if (!t.has_value()) {
            t = ray_segment_intersection(point, dir, triangle[i],
                                         triangle[i2]);
            continue;
        }

        // small distance could be preccission inconsistance
        std::optional<double> t2 = ray_segment_intersection(point, dir,
                                                            triangle[i],
                                                            triangle[i2]);
        if (t2.has_value() && *t2 > *t) t = t2;
    }
    assert(t.has_value()); // Not found intersection.
    return point + dir * (*t);
}

Vec3d calc_hit_point(const igl::Hit &          h,
                     const Vec3i &             triangle,
                     const std::vector<Vec3f> &vertices)
{
    double c1 = h.u;
    double c2 = h.v;
    double c0 = 1.0 - c1 - c2;
    Vec3d  v0 = vertices[triangle[0]].cast<double>();
    Vec3d  v1 = vertices[triangle[1]].cast<double>();
    Vec3d  v2 = vertices[triangle[2]].cast<double>();
    return v0 * c0 + v1 * c1 + v2 * c2;
}

Vec3d calc_hit_point(const igl::Hit &h, indexed_triangle_set &its)
{
    return calc_hit_point(h, its.indices[h.id], its.vertices);
}
} // namespace Private

std::string get_font_filepath() {
    std::string resource_dir = 
        std::string(TEST_DATA_DIR) + "/../../resources/";
    return resource_dir + "fonts/NotoSans-Regular.ttf";
}

#include "imgui/imstb_truetype.h"
TEST_CASE("Read glyph C shape from font, stb library calls ONLY", "[Emboss]") {
    std::string font_path = get_font_filepath();
    char  letter   = 'C';
    
    // Read  font file
    FILE *file = fopen(font_path.c_str(), "rb");
    REQUIRE(file != nullptr);
    // find size of file
    REQUIRE(fseek(file, 0L, SEEK_END) == 0);
    size_t size = ftell(file);
    REQUIRE(size != 0);
    rewind(file);
    std::vector<unsigned char> buffer(size);
    size_t count_loaded_bytes = fread((void *) &buffer.front(), 1, size, file);
    REQUIRE(count_loaded_bytes == size);

    // Use stb true type library
    int font_offset = stbtt_GetFontOffsetForIndex(buffer.data(), 0);
    REQUIRE(font_offset >= 0);
    stbtt_fontinfo font_info;
    REQUIRE(stbtt_InitFont(&font_info, buffer.data(), font_offset) != 0);    
    int unicode_letter = (int) letter;
    int glyph_index = stbtt_FindGlyphIndex(&font_info, unicode_letter);
    REQUIRE(glyph_index != 0);
    stbtt_vertex *vertices;
    int num_verts = stbtt_GetGlyphShape(&font_info, glyph_index, &vertices);
    CHECK(num_verts > 0);
}

#include <libslic3r/Utils.hpp>
TEST_CASE("Convert glyph % to model", "[Emboss]") 
{
    std::string font_path = get_font_filepath();
    char  letter   = '%';
    float flatness = 2.;

    auto font = Emboss::create_font_file(font_path.c_str());
    REQUIRE(font != nullptr);

    std::optional<Emboss::Glyph> glyph = Emboss::letter2glyph(*font, letter, flatness);
    REQUIRE(glyph.has_value());

    ExPolygons shape = glyph->shape;    
    REQUIRE(!shape.empty());

    float z_depth = 1.f;
    Emboss::ProjectZ projection(z_depth);
    indexed_triangle_set its = Emboss::polygons2model(shape, projection);

    CHECK(!its.indices.empty());    
}

TEST_CASE("Convert text with glyph cache to model", "[Emboss]")
{
    std::string font_path = get_font_filepath();
    std::string text = 
"Because Ford never learned to say his original name, \n\
his father eventually died of shame, which is still \r\n\
a terminal disease in some parts of the Galaxy.\n\r\
The other kids at school nicknamed him Ix,\n\
which in the language of Betelgeuse Five translates as\t\n\
\"boy who is not able satisfactorily to explain what a Hrung is,\n\
nor why it should choose to collapse on Betelgeuse Seven\".";
    float line_height = 10.f, depth = 2.f;

    auto font = Emboss::create_font_file(font_path.c_str());
    REQUIRE(font != nullptr);

    Emboss::FontFileWithCache ffwc(std::move(font));
    FontProp fp{line_height, depth};
    ExPolygons shapes = Emboss::text2shapes(ffwc, text.c_str(), fp);
    REQUIRE(!shapes.empty());

    Emboss::ProjectZ projection(depth);
    indexed_triangle_set its = Emboss::polygons2model(shapes, projection);
    CHECK(!its.indices.empty());
    //its_write_obj(its, "C:/data/temp/text.obj");
}

TEST_CASE("Test hit point", "[AABBTreeIndirect]")
{
    indexed_triangle_set its;
    its.vertices = {
        Vec3f(1, 1, 1),
        Vec3f(2, 10, 2),
        Vec3f(10, 0, 2),
    };
    its.indices = {Vec3i(0, 2, 1)};
    auto tree   = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        its.vertices, its.indices);

    Vec3d    ray_point(8, 1, 0);
    Vec3d    ray_dir(0, 0, 1);
    igl::Hit hit;
    AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices, tree,
                                              ray_point, ray_dir, hit);
    Vec3d hp = Private::calc_hit_point(hit, its);
    CHECK(abs(hp.x() - ray_point.x()) < .1);
    CHECK(abs(hp.y() - ray_point.y()) < .1);
}

TEST_CASE("ray segment intersection", "[MeshBoolean]")
{
    Vec2d r_point(1, 1);
    Vec2d r_dir(1, 0);

    // colinear
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(0, 0), Vec2d(2, 0)).has_value());
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, 0), Vec2d(0, 0)).has_value());

    // before ray
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(0, 0), Vec2d(0, 2)).has_value());
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(0, 2), Vec2d(0, 0)).has_value());

    // above ray
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, 2), Vec2d(2, 3)).has_value());
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, 3), Vec2d(2, 2)).has_value());

    // belove ray
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, 0), Vec2d(2, -1)).has_value());
    CHECK(!Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, -1), Vec2d(2, 0)).has_value());

    // intersection at [2,1] distance 1
    auto t1 = Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, 0), Vec2d(2, 2));
    REQUIRE(t1.has_value());
    auto t2 = Private::ray_segment_intersection(r_point, r_dir, Vec2d(2, 2), Vec2d(2, 0));
    REQUIRE(t2.has_value());

    CHECK(abs(*t1 - *t2) < std::numeric_limits<double>::epsilon());
}

TEST_CASE("triangle intersection", "[]")
{
    Vec2d                point(1, 1);
    Vec2d                dir(-1, 0);
    std::array<Vec2d, 3> triangle = {Vec2d(0, 0), Vec2d(5, 0), Vec2d(0, 5)};
    Vec2d                i = Private::get_intersection(point, dir, triangle);
    CHECK(abs(i.x()) < std::numeric_limits<double>::epsilon());
    CHECK(abs(i.y() - 1.) < std::numeric_limits<double>::epsilon());
}

#ifndef __APPLE__
#include <string>
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;
// Check function Emboss::is_italic that exist some italic and some non-italic font.
TEST_CASE("Italic check", "[Emboss]") 
{  
    std::queue<std::string> dir_paths;
#ifdef _WIN32
    dir_paths.push("C:/Windows/Fonts");
#elif defined(__linux__)
    dir_paths.push("/usr/share/fonts");
//#elif defined(__APPLE__)
//    dir_paths.push("//System/Library/Fonts");
#endif
    bool exist_italic = false;
    bool exist_non_italic = false;
    while (!dir_paths.empty()) {
        std::string dir_path = dir_paths.front();
        dir_paths.pop();
        for (const auto &entry : fs::directory_iterator(dir_path)) {
            const fs::path &act_path = entry.path();
            if (entry.is_directory()) {
                dir_paths.push(act_path.u8string());
                continue;
            }
            std::string ext = act_path.extension().u8string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext != ".ttf") continue;
            std::string path_str = act_path.u8string();
            auto        font_opt = Emboss::create_font_file(path_str.c_str());
            if (font_opt == nullptr) continue;

            unsigned int collection_number = 0;
            if (Emboss::is_italic(*font_opt, collection_number))
                exist_italic = true;
            else
                exist_non_italic = true;

            if (exist_italic && exist_non_italic) break;
            //std::cout << ((Emboss::is_italic(*font_opt)) ? "[yes] " : "[no ] ") << entry.path() << std::endl;
        }
    }
    CHECK(exist_italic);
    CHECK(exist_non_italic);
}
#endif // not __APPLE__

#if ENABLE_NEW_CGAL
#include "libslic3r/CutSurface.hpp"
TEST_CASE("Cut surface", "[]")
{
    std::string font_path = get_font_filepath();
    char        letter    = '%';
    float       flatness  = 2.;

    auto font = Emboss::create_font_file(font_path.c_str());
    REQUIRE(font != nullptr);

    std::optional<Emboss::Glyph> glyph = Emboss::letter2glyph(*font, letter,
                                                              flatness);
    REQUIRE(glyph.has_value());

    ExPolygons shape = glyph->shape;
    REQUIRE(!shape.empty());

    float            z_depth = 50.f;
    Emboss::ProjectZ projection(z_depth);

    auto object = its_make_cube(782 - 49 + 50, 724 + 10 + 50, 5);
    its_translate(object, Vec3f(49 - 25, -10 - 25, 2.5));
    auto cube2 = object; // copy
    its_translate(cube2, Vec3f(100, -40, 40));
    its_merge(object, std::move(cube2));

    auto surfaces = cut_surface(object, shape, projection);
    CHECK(!surfaces.empty());
}


#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Cartesian_converter.h>

/// <summary>
/// Distiguish point made by shape(Expolygon)
/// Referencing an ExPolygon contour plus a vertex base of the contour.
/// Used for adressing Vertex of mesh created by extrude ExPolygons
/// </summary>
struct ShapesVertexId {
    // Index of an ExPolygon in ExPolygons.
    int32_t expoly{ -1 };

    // Index of a contour in ExPolygon.
    // 0 - outer contour, >0 - hole
    int32_t contour{ -1 };

    // Base of the zero'th point of a contour in text mesh.
    // There are two vertices (front and rear) created for each contour,
    // thus there are 2x more vertices in text mesh than the number of contour points.
    int32_t vertex_base{ -1 };
};

/// <summary>
/// IntersectingElemnt
/// 
/// Adress polygon inside of ExPolygon
/// Keep information about source of vertex:
///     - from face (one of 2 possible)
///     - from edge (one of 2 possible)
/// 
/// V1~~~~V2
/// : f1 /|
/// :   / |
/// :  /e1| 
/// : /   |e2
/// :/ f2 |
/// V1'~~~V2'
/// 
/// | .. edge
/// / .. edge
/// : .. foreign edge - neighbor 
/// ~ .. no care edge - idealy should not cross model
/// V1,V1' .. projected 2d point to 3d
/// V2,V2' .. projected 2d point to 3d
/// 
/// f1 .. text_face_1 (triangle face made by side of shape contour)
/// f2 .. text_face_2
/// e1 .. text_edge_1 (edge on side of face made by side of shape contour)
/// e2 .. text_edge_2
/// 
/// </summary>
struct IntersectingElemnt
{
    // Index into vector of ShapeVertexId
    // describe point on shape contour
    int32_t vertex_index{-1};

    // index of point in Polygon contour
    int32_t point_index{-1};

    // vertex or edge ID, where edge ID is the index of the source point.
    // There are 4 consecutive indices generated for a single glyph edge:
    // 0th - 1st text edge (straight)
    // 1th - 1st text face
    // 2nd - 2nd text edge (diagonal)
    // 3th - 2nd text face
    // Type of intersecting element from extruded shape( 3d )
    enum class Type {
        edge_1 = 0,
        face_1 = 1,
        edge_2 = 2,
        face_2 = 3,

        undefined = 4
    } type = Type::undefined;
};

namespace Slic3r::MeshBoolean::cgal2 {

    namespace CGALProc = CGAL::Polygon_mesh_processing;
    namespace CGALParams = CGAL::Polygon_mesh_processing::parameters;

    using EpicKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
    using _EpicMesh = CGAL::Surface_mesh<EpicKernel::Point_3>;
//    using EpecKernel = CGAL::Exact_predicates_exact_constructions_kernel;
//    using _EpecMesh = CGAL::Surface_mesh<EpecKernel::Point_3>;

    using CGALMesh = _EpicMesh;
            
    /// <summary>
    /// Convert triangle mesh model to CGAL Surface_mesh
    /// Add property map for source face index
    /// </summary>
    /// <param name="its">Model</param>
    /// <param name="face_map_name">Property map name for store conversion from CGAL face to index to its</param>
    /// <returns>CGAL mesh - half edge mesh</returns>
    CGALMesh to_cgal(const indexed_triangle_set &its,
                     const std::string          &face_map_name)
    {
        CGALMesh result;
        if (its.empty()) return result;

        const std::vector<stl_vertex>                  &V = its.vertices;
        const std::vector<stl_triangle_vertex_indices> &F = its.indices;

        // convert from CGAL face to its face
        auto face_map = result.add_property_map<CGALMesh::Face_index, int32_t>(face_map_name).first;

        size_t vertices_count = V.size();
        size_t edges_count    = (F.size() * 3) / 2;
        size_t faces_count    = F.size();
        result.reserve(vertices_count, edges_count, faces_count);

        for (auto &v : V)
            result.add_vertex(typename CGALMesh::Point{v.x(), v.y(), v.z()});

        using VI = typename CGALMesh::Vertex_index;
        for (auto &f : F)
        {
            auto fid = result.add_face(VI(f(0)), VI(f(1)), VI(f(2)));
            // index of face in source triangle mesh
            int32_t index = static_cast<int32_t>(&f - &F.front());
            face_map[fid] = index;
        }

        return result;
    }

    /// <summary>
    /// Covert 2d shape (e.g. Glyph) to CGAL model
    /// </summary>
    /// <param name="shape">2d shape to project</param>
    /// <param name="projection">Define transformation 2d point into 3d</param>
    /// <param name="shape_id">Identify shape</param>
    /// <param name="edge_shape_map_name">Name of property map to store conversion from edge to contour</param>
    /// <param name="face_shape_map_name">Name of property map to store conversion from face to contour</param>
    /// <param name="contour_indices">Identify point on shape contour</param>
    /// <returns>CGAL model of extruded shape</returns>
    CGALMesh to_cgal(const ExPolygons                &shape,
                     const Slic3r::Emboss::IProject &projection,
                     int32_t                         shape_id,
                     const std::string              &edge_shape_map_name,
                     const std::string              &face_shape_map_name,
                     std::vector<ShapesVertexId>      &contour_indices)
    {
        CGALMesh result;
        if (shape.empty()) return result;
        
        auto edge_shape_map = result.add_property_map<CGALMesh::Edge_index, IntersectingElemnt>(edge_shape_map_name).first;
        auto face_shape_map = result.add_property_map<CGALMesh::Face_index, IntersectingElemnt>(face_shape_map_name).first;

        std::vector<CGALMesh::Vertex_index> indices;
        auto insert_contour = [&projection, &indices , &result, &contour_indices, &edge_shape_map, &face_shape_map](const Polygon& polygon, int32_t iexpoly, int32_t id) {
            indices.clear();
            indices.reserve(polygon.points.size() * 2);
            size_t num_vertices_old = result.number_of_vertices();
            int32_t vertex_index = static_cast<int32_t>(contour_indices.size());
            contour_indices.push_back({iexpoly, id, int32_t(num_vertices_old) });
            for (const Point& p2 : polygon.points) {
                auto p = projection.project(p2);
                auto vi = result.add_vertex(typename CGALMesh::Point{ p.first.x(), p.first.y(), p.first.z() });
                assert((size_t)vi == indices.size() + num_vertices_old);
                indices.emplace_back(vi);
                vi = result.add_vertex(typename CGALMesh::Point{ p.second.x(), p.second.y(), p.second.z() });
                assert((size_t)vi == indices.size() + num_vertices_old);
                indices.emplace_back(vi);
            }
            int32_t contour_index = 0;
            for (int32_t i = 0; i < int32_t(indices.size()); i += 2) {
                int32_t j = (i + 2) % int32_t(indices.size());
                auto find_edge = [&result](CGALMesh::Face_index fi, CGALMesh::Vertex_index from, CGALMesh::Vertex_index to) {
                    CGALMesh::Halfedge_index hi = result.halfedge(fi);
                    for (; result.target(hi) != to; hi = result.next(hi));
                    assert(result.source(hi) == from);
                    assert(result.target(hi) == to);
                    return hi;
                };
                auto fi = result.add_face(indices[i], indices[i + 1], indices[j]);
                edge_shape_map[result.edge(find_edge(fi, indices[i], indices[i + 1]))] = 
                    IntersectingElemnt{vertex_index, contour_index, IntersectingElemnt::Type::edge_1};
                face_shape_map[fi] =                     
                    IntersectingElemnt{vertex_index, contour_index, IntersectingElemnt::Type::face_1};
                edge_shape_map[result.edge(find_edge(fi, indices[i + 1], indices[j]))] =
                    IntersectingElemnt{vertex_index, contour_index, IntersectingElemnt::Type::edge_2};
                face_shape_map[result.add_face(indices[j], indices[i + 1], indices[j + 1])] =                     
                    IntersectingElemnt{vertex_index, contour_index, IntersectingElemnt::Type::face_2};
                ++contour_index;
            }
        };

        size_t count_point = count_points(shape);
        result.reserve(result.number_of_vertices() + 2 * count_point, result.number_of_edges() + 4 * count_point, result.number_of_faces() + 2 * count_point);

        // Identify polygon
        // (contour_id > 0) are holes
        for (const auto &s : shape) {
            size_t contour_id = 0;
            insert_contour(s.contour, shape_id, contour_id++);
            for (const Polygon &hole : s.holes)
                insert_contour(hole, shape_id, contour_id++);
            ++shape_id;
        }
        return result;
    }
}
#include "libslic3r/TriangleMesh.hpp"

//// 1 ////

// Question store(1) Or calculate on demand(2) ??
// (1) type: vector <vector<vertex indices>>
// (1) Needs recalculation when merge and propagation togewther with its
// (2) Could appear surface mistakes(need calc - all half edges) 
// (2) NO need of trace cut outline and connect it with letter conture points 

/// <summary>
/// Cut surface shape from source model
/// </summary>
/// <param name="source">Input source mesh</param>
/// <param name="shape">Input 2d shape to cut from surface</param>
/// <param name="projection">Define transformation from 2d to 3d</param>
/// <returns>Cutted surface, Its do not represent Volume</returns>
indexed_triangle_set cut_shape(const indexed_triangle_set &source,
                               const ExPolygon            &shape,
                               const Emboss::IProject     &projection)
{
    throw std::exception("NOT implemented yet");
    return {};
}

/// <summary>
/// Cut surface shape from source model
/// </summary>
/// <param name="source">Input source mesh</param>
/// <param name="shapes">Input 2d shape to cut from surface</param>
/// <param name="projection">Define transformation from 2d to 3d</param>
/// <returns>Cutted surface, Its do not represent Volume</returns>
indexed_triangle_set cut_shape(const indexed_triangle_set &source,
                               const ExPolygons           &shapes,
                               const Emboss::IProject     &projection)
{
    indexed_triangle_set result;
    for (const ExPolygon &shape : shapes)
        its_merge(result, cut_shape(source, shape, projection));
    return result;
}

using MyMesh = Slic3r::MeshBoolean::cgal2::CGALMesh;

// First Idea //// 1 ////
// Use source model to modify ONLY surface of text ModelVolume

// Second Idea
// Store original its inside of text configuration[optional]
// Cause problem with next editation of object -> cut, simplify, Netfabb, Hollow, ...(transform original vertices)
TEST_CASE("Emboss extrude cut", "[Emboss-Cut]")
{
    std::string font_path = get_font_filepath();
    char  letter   = '%';
    float flatness = 2.;

    auto font = Emboss::create_font_file(font_path.c_str());
    REQUIRE(font != nullptr);

    std::optional<Emboss::Glyph> glyph = Emboss::letter2glyph(*font, letter,
                                                              flatness);
    REQUIRE(glyph.has_value());

    ExPolygons shape = glyph->shape;
    REQUIRE(!shape.empty());

    float            z_depth = 50.f;
    Emboss::ProjectZ projection(z_depth);

#if 0
    indexed_triangle_set text = Emboss::polygons2model(shape, projection);
    BoundingBoxf3 bbox = bounding_box(text);

    CHECK(!text.indices.empty());
#endif
    
    auto cube = its_make_cube(782 - 49 + 50, 724 + 10 + 50, 5);
    its_translate(cube, Vec3f(49 - 25, -10 - 25, 2.5));
    auto cube2 = cube;
//    its_translate(cube2, Vec3f(0, 0, 40));
    its_translate(cube2, Vec3f(100, -40, 40));
    its_merge(cube, std::move(cube2));

    //cube = its_make_sphere(350., 1.);
    //for (auto &face : cube2.indices)
    //    for (int i = 0; i < 3; ++ i)
    //        face(i) += int(cube.vertices.size());
    //append(cube.vertices, cube2.vertices);
    //append(cube.indices, cube2.indices);

    using MyMesh = Slic3r::MeshBoolean::cgal2::CGALMesh;
    
    // name of CGAL property map for store source object face id - index into its.indices
    std::string face_map_name = "f:face_map";
    std::string face_type_map_name = "f:type";
    // identify glyph for intersected vertex
    std::string vert_shape_map_name = "v:glyph_id";
    MyMesh cgal_object = MeshBoolean::cgal2::to_cgal(cube, face_map_name);
    auto& face_map = cgal_object.property_map<MyMesh::Face_index, int32_t>(face_map_name).first;
    auto& vert_shape_map = cgal_object.add_property_map<MyMesh::Vertex_index, IntersectingElemnt>(vert_shape_map_name).first;

    std::string edge_shape_map_name = "e:glyph_id";
    std::string face_shape_map_name = "f:glyph_id";
    std::vector<ShapesVertexId> glyph_contours;

    MyMesh cgal_shape = MeshBoolean::cgal2::to_cgal(shape, projection, 0, edge_shape_map_name, face_shape_map_name, glyph_contours);    

    auto& edge_shape_map = cgal_shape.property_map<MyMesh::Edge_index, IntersectingElemnt>(edge_shape_map_name).first;
    auto& face_shape_map = cgal_shape.property_map<MyMesh::Face_index, IntersectingElemnt>(face_shape_map_name).first;

    // bool map for affected edge
    using d_prop_bool = CGAL::dynamic_edge_property_t<bool>;
    using ecm_it = boost::property_map<MyMesh, d_prop_bool>::SMPM;
    using EcmType = CGAL::internal::Dynamic<MyMesh, ecm_it>;
    EcmType ecm = get(d_prop_bool(), cgal_object);
    
    struct Visitor {
        const MyMesh &object;
        const MyMesh &shape;
        // Properties of the shape mesh:
        MyMesh::Property_map<CGAL::SM_Edge_index, IntersectingElemnt> edge_shape_map;
        MyMesh::Property_map<CGAL::SM_Face_index, IntersectingElemnt> face_shape_map;
        // Properties of the object mesh.
        MyMesh::Property_map<CGAL::SM_Face_index, int32_t> face_map;
        MyMesh::Property_map<CGAL::SM_Vertex_index, IntersectingElemnt> vert_shape_map;

        typedef boost::graph_traits<MyMesh> GT;
        typedef typename GT::face_descriptor face_descriptor;
        typedef typename GT::halfedge_descriptor halfedge_descriptor;
        typedef typename GT::vertex_descriptor vertex_descriptor;

        int32_t source_face_id = -1;
        void before_subface_creations(face_descriptor f_old, MyMesh& mesh)
        {
            assert(&mesh == &object);
            source_face_id = face_map[f_old];
        }
        // it is called multiple times for one source_face_id
        void after_subface_created(face_descriptor f_new, MyMesh &mesh)
        {
            assert(&mesh == &object);
            assert(source_face_id != -1);
            face_map[f_new] = source_face_id;
        }

        std::vector<const IntersectingElemnt*> intersection_point_glyph;

        // Intersecting an edge hh_edge from tm_edge with a face hh_face of tm_face.
        void intersection_point_detected(
            // ID of the intersection point, starting at 0. Ids are consecutive.
            std::size_t         i_id,
            // Dimension of a simplex part of face(hh_face) that is intersected by hh_edge:
            // 0 for vertex: target(hh_face)
            // 1 for edge: hh_face
            // 2 for the interior of face: face(hh_face)
            int                 simplex_dimension,
            // Edge of tm_edge, see edge_source_coplanar_with_face & edge_target_coplanar_with_face whether any vertex of hh_edge is coplanar with face(hh_face).
            halfedge_descriptor hh_edge,
            // Vertex, halfedge or face of tm_face intersected by hh_edge, see comment at simplex_dimension.
            halfedge_descriptor hh_face,
            // Mesh containing hh_edge
            const MyMesh& tm_edge,
            // Mesh containing hh_face
            const MyMesh& tm_face,
            // source(hh_edge) is coplanar with face(hh_face).
            bool                edge_source_coplanar_with_face,
            // target(hh_edge) is coplanar with face(hh_face).
            bool                edge_target_coplanar_with_face)
        {
            if (i_id <= intersection_point_glyph.size()) {
                intersection_point_glyph.reserve(Slic3r::next_highest_power_of_2(i_id + 1));
                intersection_point_glyph.resize(i_id + 1);
            }

            const IntersectingElemnt* glyph = nullptr;
            if (&tm_face == &shape) {
                assert(&tm_edge == &object);
                switch (simplex_dimension) {
                case 1:
                    // edge x edge intersection
                    glyph = &edge_shape_map[shape.edge(hh_face)];
                    break;
                case 2:
                    // edge x face intersection
                    glyph = &face_shape_map[shape.face(hh_face)];
                    break;
                default:
                    assert(false);
                }
                if (edge_source_coplanar_with_face)
                    vert_shape_map[object.source(hh_edge)] = *glyph;
                if (edge_target_coplanar_with_face)
                    vert_shape_map[object.target(hh_edge)] = *glyph;
            } else {
                assert(&tm_edge == &shape && &tm_face == &object);
                assert(!edge_source_coplanar_with_face);
                assert(!edge_target_coplanar_with_face);
                glyph = &edge_shape_map[shape.edge(hh_edge)];
                if (simplex_dimension == 0)
                    vert_shape_map[object.target(hh_face)] = *glyph;
            }
            intersection_point_glyph[i_id] = glyph;
        }

        void new_vertex_added(std::size_t node_id, vertex_descriptor vh, const MyMesh &tm)
        {
            assert(&tm == &object);
            assert(node_id < intersection_point_glyph.size());
            const IntersectingElemnt * glyph = intersection_point_glyph[node_id];
            assert(glyph != nullptr);
            assert(glyph->vertex_index != -1);
            assert(glyph->point_index != -1);
            vert_shape_map[vh] = glyph ? *glyph : IntersectingElemnt{};
        }

        void after_subface_creations(MyMesh&) {}
        void before_subface_created(MyMesh&) {}
        void before_edge_split(halfedge_descriptor /* h */, MyMesh& /* tm */) {}
        void edge_split(halfedge_descriptor /* hnew */, MyMesh& /* tm */) {}
        void after_edge_split() {}
        void add_retriangulation_edge(halfedge_descriptor /* h */, MyMesh& /* tm */) {}
    } visitor{cgal_object, cgal_shape, edge_shape_map, face_shape_map,
              face_map, vert_shape_map};

    const auto& p = CGAL::Polygon_mesh_processing::parameters::throw_on_self_intersection(false).visitor(visitor).edge_is_constrained_map(ecm);
    const auto& q = CGAL::Polygon_mesh_processing::parameters::do_not_modify(true);
    //    CGAL::Polygon_mesh_processing::corefine(cgal_object, cgalcube2, p, p);

    CGAL::Polygon_mesh_processing::corefine(cgal_object, cgal_shape, p, q);

    enum class SideType {
        // face inside of the cutted shape
        inside,
        // face outside of the cutted shape
        outside,
        // face without constrained edge (In or Out)
        not_constrained
    };
    auto side_type_map = cgal_object.add_property_map<MyMesh::Face_index, SideType>("f:side").first;
    for (auto fi : cgal_object.faces()) {
        SideType side_type = SideType::not_constrained;
        auto hi_end = cgal_object.halfedge(fi);
        auto hi = hi_end;
        do {
            CGAL::SM_Edge_index edge_index = cgal_object.edge(hi);
            // is edge new created - constrained?
            if (get(ecm, edge_index)) {
                // This face has a constrained edge.
                IntersectingElemnt shape_from = vert_shape_map[cgal_object.source(hi)];
                IntersectingElemnt shape_to = vert_shape_map[cgal_object.target(hi)];
                assert(shape_from.vertex_index != -1);
                assert(shape_from.vertex_index == shape_to.vertex_index);
                assert(shape_from.point_index != -1);
                assert(shape_to.point_index != -1);
                
                const ShapesVertexId &vertex_index = glyph_contours[shape_from.vertex_index];
                const ExPolygon &expoly  = shape[vertex_index.expoly];
                const Polygon &contour = vertex_index.contour == 0 ? expoly.contour : expoly.holes[vertex_index.contour - 1];
                bool is_inside = false;
                
                // 4 type 
                // index into contour
                int32_t i_from = shape_from.point_index;
                int32_t i_to = shape_to.point_index;
                if (i_from == i_to && shape_from.type == shape_to.type) {

                    const auto &p = cgal_object.point(cgal_object.target(cgal_object.next(hi)));

                    int i = i_from * 2;
                    int j = (i_from + 1 == int(contour.size())) ? 0 : i + 2;

                    i += vertex_index.vertex_base;
                    j += vertex_index.vertex_base;

                    auto abcp =
                        shape_from.type == IntersectingElemnt::Type::face_1 ?
                            CGAL::orientation(
                                cgal_shape.point(CGAL::SM_Vertex_index(i)),
                                cgal_shape.point(CGAL::SM_Vertex_index(i + 1)),
                                cgal_shape.point(CGAL::SM_Vertex_index(j)), p) :
                            // shape_from.type == IntersectingElemnt::Type::face_2
                            CGAL::orientation(
                                cgal_shape.point(CGAL::SM_Vertex_index(j)),
                                cgal_shape.point(CGAL::SM_Vertex_index(i + 1)),
                                cgal_shape.point(CGAL::SM_Vertex_index(j + 1)),
                                p);
                    is_inside = abcp == CGAL::POSITIVE;
                } else if (i_from < i_to || (i_from == i_to && shape_from.type < shape_to.type)) {
                    bool is_last = i_from == 0 && (i_to + 1) == contour.size();
                    if (!is_last) is_inside = true;
                } else { // i_from > i_to || (i_from == i_to && shape_from.type > shape_to.type)
                    bool is_last = i_to == 0 && (i_from + 1) == contour.size();
                    if (is_last) is_inside = true;
                }

                if (is_inside) {
                    // Is this face oriented towards p or away from p?
                    const auto &a = cgal_object.point(cgal_object.source(hi));
                    const auto &b = cgal_object.point(cgal_object.target(hi));
                    const auto &c = cgal_object.point(cgal_object.target(cgal_object.next(hi)));
                    //FIXME prosim nahrad skutecnou projekci.
                    //projection.project()
                    const auto  p = a + MeshBoolean::cgal2::EpicKernel::Vector_3(0, 0, 10);
                    auto abcp = CGAL::orientation(a, b, c, p);
                    if (abcp == CGAL::POSITIVE)
                        side_type = SideType::inside;
                    else
                        is_inside = false;
                } 
                if (!is_inside) side_type = SideType::outside;                
                break;
            }
            // next half edge index inside of face
            hi = cgal_object.next(hi);
        } while (hi != hi_end);
        side_type_map[fi] = side_type;
    }
    
    // debug output
    auto face_colors = cgal_object.add_property_map<MyMesh::Face_index, CGAL::Color>("f:color").first;    
    for (auto fi : cgal_object.faces()) { 
        auto &color = face_colors[fi];
        switch (side_type_map[fi]) {
        case SideType::inside: color = CGAL::Color{255, 0, 0}; break;
        case SideType::outside: color = CGAL::Color{255, 0, 255}; break;
        case SideType::not_constrained: color = CGAL::Color{0, 255, 0}; break;
        }
    }
    CGAL::IO::write_OFF("c:\\data\\temp\\constrained.off", cgal_object);
    
    // Seed fill the other faces inside the region.
    for (Visitor::face_descriptor fi : cgal_object.faces()) {
        if (side_type_map[fi] != SideType::not_constrained) continue;

        // check if neighbor face is inside
        Visitor::halfedge_descriptor hi     = cgal_object.halfedge(fi);
        Visitor::halfedge_descriptor hi_end = hi; 

        bool has_inside_neighbor = false;
        std::vector<MyMesh::Face_index> queue;
        do {
            Visitor::face_descriptor fi_opposite = cgal_object.face(cgal_object.opposite(hi));
            SideType side = side_type_map[fi_opposite];
            if (side == SideType::inside) {
                has_inside_neighbor = true;
            } else if (side == SideType::not_constrained) {
                queue.emplace_back(fi_opposite);
            }
            hi = cgal_object.next(hi);
        } while (hi != hi_end);
        if (!has_inside_neighbor) continue;
        side_type_map[fi] = SideType::inside;
        while (!queue.empty()) {
            Visitor::face_descriptor fi = queue.back();
            queue.pop_back();
            // Do not fill twice
            if (side_type_map[fi] == SideType::inside) continue;
            side_type_map[fi] = SideType::inside;

            // check neighbor triangle
            Visitor::halfedge_descriptor hi     = cgal_object.halfedge(fi);
            Visitor::halfedge_descriptor hi_end = hi; 
            do {
                Visitor::face_descriptor fi_opposite = cgal_object.face(cgal_object.opposite(hi));
                SideType side = side_type_map[fi_opposite];
                if (side == SideType::not_constrained) 
                    queue.emplace_back(fi_opposite);                
                hi = cgal_object.next(hi);
            } while (hi != hi_end);
        }            
    }

    // debug output
    for (auto fi : cgal_object.faces()) { 
        auto &color = face_colors[fi];
        switch (side_type_map[fi]) {
        case SideType::inside: color = CGAL::Color{255, 0, 0}; break;
        case SideType::outside: color = CGAL::Color{255, 0, 255}; break;
        case SideType::not_constrained: color = CGAL::Color{0, 255, 0}; break;
        }
    }
    CGAL::IO::write_OFF("c:\\data\\temp\\filled.off", cgal_object);

    // Mapping of its_extruded faces to source faces.
    enum class FaceState : int8_t {
        Unknown         = -1,
        Unmarked        = -2,
        UnmarkedSplit   = -3,
        Marked          = -4,
        MarkedSplit     = -5,
        UnmarkedEmitted = -6,
    };
    std::vector<FaceState> face_states(cube.indices.size(), FaceState::Unknown);
    for (auto fi_seed : cgal_object.faces()) {
        FaceState &state = face_states[face_map[fi_seed]];
        bool is_face_inside = side_type_map[fi_seed] == SideType::inside;
        switch (state) {
        case FaceState::Unknown:
            state = is_face_inside ? FaceState::Marked : FaceState::Unmarked;
            break;
        case FaceState::Unmarked:
        case FaceState::UnmarkedSplit:
            state = is_face_inside ? FaceState::MarkedSplit : FaceState::UnmarkedSplit;
            break;
        case FaceState::Marked:
        case FaceState::MarkedSplit:
            state = FaceState::MarkedSplit;
            break;
        default:
            assert(false);
        }
    }

    indexed_triangle_set its_extruded;
    its_extruded.indices.reserve(cgal_object.number_of_faces());
    its_extruded.vertices.reserve(cgal_object.number_of_vertices());
    // Mapping of its_extruded vertices (original and offsetted) to cgalcuble's vertices.
    std::vector<std::pair<int32_t, int32_t>> map_vertices(cgal_object.number_of_vertices(), std::pair<int32_t, int32_t>{-1, -1});

    Vec3f extrude_dir { 0, 0, 5.f };
    for (auto fi : cgal_object.faces()) {
        const int32_t   source_face_id = face_map[fi];
        const FaceState state          = face_states[source_face_id];
        assert(state == FaceState::Unmarked || state == FaceState::UnmarkedSplit || state == FaceState::UnmarkedEmitted ||
               state == FaceState::Marked || state == FaceState::MarkedSplit);
        if (state == FaceState::UnmarkedEmitted) continue; // Already emitted.

        if (state == FaceState::Unmarked || 
            state == FaceState::UnmarkedSplit) {
            // Just copy the unsplit source face.
            const Vec3i source_vertices = cube.indices[source_face_id];
            Vec3i       target_vertices;
            for (int i = 0; i < 3; ++i) {
                target_vertices(i) = map_vertices[source_vertices(i)].first;
                if (target_vertices(i) == -1) {
                    map_vertices[source_vertices(i)].first = target_vertices(i) = int(its_extruded.vertices.size());
                    its_extruded.vertices.emplace_back(cube.vertices[source_vertices(i)]);
                }
            }
            its_extruded.indices.emplace_back(target_vertices);
            face_states[source_face_id] = FaceState::UnmarkedEmitted;
            continue; // revert modification
        } 

        auto hi = cgal_object.halfedge(fi);
        auto hi_prev = cgal_object.prev(hi);
        auto hi_next = cgal_object.next(hi);
        const Vec3i source_vertices{ 
            int((std::size_t)cgal_object.target(hi)), 
            int((std::size_t)cgal_object.target(hi_next)), 
            int((std::size_t)cgal_object.target(hi_prev)) };
        Vec3i target_vertices;
        if (side_type_map[fi] != SideType::inside) {
            // Copy the face.
            Vec3i target_vertices;
            for (int i = 0; i < 3; ++ i) {
                target_vertices(i) = map_vertices[source_vertices(i)].first;
                if (target_vertices(i) == -1) {
                    map_vertices[source_vertices(i)].first = target_vertices(i) = int(its_extruded.vertices.size());
                    const auto &p = cgal_object.point(cgal_object.target(hi));
                    its_extruded.vertices.emplace_back(p.x(), p.y(), p.z());
                }
                hi = cgal_object.next(hi);
            }
            its_extruded.indices.emplace_back(target_vertices);
            continue; // copy splitted triangle
        }
        
        // Extrude the face. Neighbor edges separating extruded face from
        // non-extruded face will be extruded.
        bool  boundary_vertex[3] = {false, false, false};
        Vec3i target_vertices_extruded{-1, -1, -1};
        for (int i = 0; i < 3; ++i) {
            if (side_type_map[cgal_object.face(cgal_object.opposite(hi))] != SideType::inside)
                // Edge separating extruded / non-extruded region.
                boundary_vertex[i] = true;
            hi = cgal_object.next(hi);
        }

        for (int i = 0; i < 3; ++i) {
            target_vertices_extruded(i) = map_vertices[source_vertices(i)].second;
            if (target_vertices_extruded(i) == -1) {
                map_vertices[source_vertices(i)].second =
                    target_vertices_extruded(i) = int(
                        its_extruded.vertices.size());
                const auto &p = cgal_object.point(cgal_object.target(hi));
                its_extruded.vertices.emplace_back(
                    Vec3f{float(p.x()), float(p.y()), float(p.z())} +
                    extrude_dir);
            }
            if (boundary_vertex[i]) {
                target_vertices(i) = map_vertices[source_vertices(i)].first;
                if (target_vertices(i) == -1) {
                    map_vertices[source_vertices(i)].first = target_vertices(
                        i)        = int(its_extruded.vertices.size());
                    const auto &p = cgal_object.point(cgal_object.target(hi));
                    its_extruded.vertices.emplace_back(p.x(), p.y(), p.z());
                }
            }
            hi = cgal_object.next(hi);
        }
        its_extruded.indices.emplace_back(target_vertices_extruded);
        // Add the sides.
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            assert(target_vertices_extruded[i] != -1 &&
                   target_vertices_extruded[j] != -1);
            if (boundary_vertex[i] && boundary_vertex[j]) {
                assert(target_vertices[i] != -1 && target_vertices[j] != -1);
                its_extruded.indices.emplace_back(
                    Vec3i{target_vertices[i], target_vertices[j],
                          target_vertices_extruded[i]});
                its_extruded.indices.emplace_back(
                    Vec3i{target_vertices_extruded[i], target_vertices[j],
                          target_vertices_extruded[j]});
            }
        }
    }

    its_write_obj(its_extruded, "c:\\data\\temp\\text-extruded.obj");

    indexed_triangle_set edges_its;
    std::vector<Vec3f>   edges_its_colors;
    for (auto ei : cgal_object.edges())
        if (cgal_object.is_valid(ei)) {
            const auto &p1 = cgal_object.point(cgal_object.vertex(ei, 0));
            const auto &p2 = cgal_object.point(cgal_object.vertex(ei, 1));
            bool constrained = get(ecm, ei);
            Vec3f color = constrained ? Vec3f{ 1.f, 0, 0 } : Vec3f{ 0, 1., 0 };
            edges_its.indices.emplace_back(Vec3i(edges_its.vertices.size(), edges_its.vertices.size() + 1, edges_its.vertices.size() + 2));
            edges_its.vertices.emplace_back(Vec3f(p1.x(), p1.y(), p1.z()));
            edges_its.vertices.emplace_back(Vec3f(p2.x(), p2.y(), p2.z()));
            edges_its.vertices.emplace_back(Vec3f(p2.x(), p2.y(), p2.z() + 0.001));
            edges_its_colors.emplace_back(color);
            edges_its_colors.emplace_back(color);
            edges_its_colors.emplace_back(color);
        }
    its_write_obj(edges_its, edges_its_colors, "c:\\data\\temp\\corefined-edges.obj");

//    MeshBoolean::cgal::minus(cube, cube2);

//    REQUIRE(!MeshBoolean::cgal::does_self_intersect(cube));
}
#endif // ENABLE_NEW_CGAL
