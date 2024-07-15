#ifndef VIEWPORT_PICKER_H
#define VIEWPORT_PICKER_H
#include <zeno/utils/vec.h>
#include <viewport/zenovis.h>

#include <QtWidgets>

#include <cfloat>
#include <optional>
#include <unordered_set>

#define CMP(x, y) \
	(fabsf(x - y) <= FLT_EPSILON * fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y))))

class ViewportWidget;

namespace zeno {

enum class SELECTION_MODE {
    NORMAL,
    APPEND,
    REMOVE,
};

std::optional<float> ray_box_intersect(
    zeno::vec3f const &bmin,
    zeno::vec3f const &bmax,
    zeno::vec3f const &ray_pos,
    zeno::vec3f const &ray_dir
);

bool test_in_selected_bounding(
    QVector3D centerWS,
    QVector3D cam_posWS,
    QVector3D left_normWS,
    QVector3D right_normWS,
    QVector3D up_normWS,
    QVector3D down_normWS
);

class Picker
{
public:
    Picker(ViewportWidget* pViewport);
    void initialize();
    void pick(int x, int y);
    void pick(int x0, int y0, int x1, int y1, SELECTION_MODE mode = SELECTION_MODE::NORMAL);
    void pick_depth(int x, int y);
    std::string just_pick_prim(int x, int y);
    const std::unordered_set<std::string>& get_picked_prims();
    void sync_to_scene();
    void load_from_str(const std::string& str, zenovis::PICK_MODE mode, SELECTION_MODE sel_mode);
    void focus(const std::string& prim_name);
    void clear();
    void set_picked_depth_callback(std::function<void(float, int, int)>);
    void set_picked_elems_callback(std::function<void(std::unordered_map<std::string, std::unordered_set<int>>&)>);
    bool get_draw_special_buffer_mode() const;
    void set_draw_special_buffer_mode(bool enable);

private:
    zenovis::Scene* get_scene() const;

    std::unique_ptr<zenovis::IPicker> picker;
    
    ViewportWidget* m_pViewport;

    std::function<void(float, int, int)> picked_depth_callback;
    std::function<void(std::unordered_map<std::string, std::unordered_set<int>>&)> picked_elems_callback;

    std::unordered_set<std::string> selected_prims;
    std::unordered_map<std::string, std::unordered_set<int>> selected_elements;

    bool draw_special_buffer_mode;
};

}

#endif //VIEWPORT_PICKER_H
