#include <zeno/core/INode.h>
#include <zeno/core/IObject.h>
#include <zeno/core/Graph.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/core/reflectdef.h>
#include <zeno/formula/zfxexecute.h>
#include <zeno/core/FunctionManager.h>
#include <zeno/types/GeometryObject.h>
#include "zeno_types/reflect/reflection.generated.hpp"


namespace zeno
{
    struct ZDEFNODE() ForEachBegin : INode
    {
        ReflectCustomUI m_uilayout = {
            _ObjectGroup {
                {
                    _ObjectParam {"init_object", "Initial Object", Socket_Clone},
                }
            },
            //����������Բ�����ʽ���ص��ⲿ����
            _ObjectGroup {
                {
                    //���ַ���Ĭ��mapping�� apply�����ֵ
                }
            },
            //����ֵ��Ϣ��
            _ObjectParam {
                "", "Output Object", Socket_Output
            },
            _ParamTab {
            },
            _ParamGroup {
            }
        };

        std::shared_ptr<INode> get_foreachend() {
            std::shared_ptr<Graph> graph = this->getGraph().lock();
            std::shared_ptr<INode> foreach_end = graph->getNode(m_foreach_end_path);
            if (!foreach_end) {
                throw makeError<KeyError>("foreach_end_path", "the path of foreach_end_path is not exist");
            }
            return foreach_end;
        }

        std::shared_ptr<IObject> apply(std::shared_ptr<IObject> init_object) {
            _out_iteration = m_current_iteration;
            auto foreach_end = get_foreachend();

            if (m_fetch_mehod == "Initial Object") {
                //��foreachend�ǵ���object����container,�����container���͵�ȡelementԪ��
                std::string itemethod = zeno::reflect::any_cast<std::string>(foreach_end->get_defl_value("Iterate Method"));
                if (itemethod == "By Count") {
                    return init_object;
                }
                else if (itemethod == "By Container") {
                    //TODO: Ŀǰֻ֧��list��������֧��dict
                    if (auto spList = std::dynamic_pointer_cast<ListObject>(init_object)) {
                        int n = spList->size();
                        if (m_current_iteration >= 0 && m_current_iteration < n) {
                            zany elemObj = spList->get(m_current_iteration);
                            return elemObj;
                        }
                        else {
                            throw makeError<UnimplError>("current iteration on foreach begin exceeds the range of Listobject");
                        }
                    }
                    else {
                        throw makeError<UnimplError>("Only support ListObject on Initial Object when select `By Container` mode in foreach_end");
                    }
                }
                else {
                    throw makeError<UnimplError>("Only support `By Count` and `By Container` mode.");
                }
            }
            else if (m_fetch_mehod == "From Last Feedback") {
                int startValue = zeno::reflect::any_cast<int>(foreach_end->get_defl_value("Start Value"));
                if (startValue == m_current_iteration) {
                    return init_object;
                }
                else {
                    std::shared_ptr<IObject> outputObj = foreach_end->get_iterate_object();
                    //outputObj of last iteration as a feedback to next procedure.
                    return outputObj;
                }
            }
            else if (m_fetch_mehod == "Element of Object") {
                //TODO
                return nullptr;
            }
            return nullptr;
        }

        int get_current_iteration() {
            int current_iteration = zeno::reflect::any_cast<int>(get_defl_value("Current Iteration"));
            return current_iteration;
        }

        void update_iteration(int new_iteration) {
            m_current_iteration = new_iteration;
            //����������������ִ�У�ִ��Ȩ�������ⲿGraph����
            zeno::reflect::Any oldvalue;
            update_param_impl("Current Iteration", m_current_iteration, oldvalue);
        }

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Fetch Method", Control = zeno::Combobox, ComboBoxItems = ("Initial Object", "From Last Feedback", "Element of Object"))
        std::string m_fetch_mehod = "From Last Feedback";

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "ForEachEnd Path")
        std::string m_foreach_end_path;

        //��ǰ����ֵ�ⲿ�����޸ģ����ɱ������������ã���˻�����Ϊ��ʽ��������Ȼ����һ�ֿ��ܣ�����֧������output����������ǰ��û���������
        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Current Iteration", InnerSocket = 1)
        int m_current_iteration = 0;

        ZPROPERTY(Role = zeno::Role_OutputPrimitive, DisplayName = "Current Iteration")
        int _out_iteration = 0;
    };


    struct ZDEFNODE() ForEachEnd : INode
    {
        ReflectCustomUI m_uilayout = {
            _ObjectGroup {
                {
                    _ObjectParam {"iterate_object", "Iterate Object", Socket_Clone},
                }
            },
            //����������Բ�����ʽ���ص��ⲿ����
            _ObjectGroup {
                {
                    //���ַ���Ĭ��mapping�� apply�����ֵ
                }
            },
            //����ֵ��Ϣ��
            _ObjectParam {
                "", "Output Object", Socket_Output
            },
            _ParamTab {
            },
            _ParamGroup {
            }
        };

        ForEachEnd() : m_collect_objs(std::make_shared<ListObject>()) {

        }

        std::shared_ptr<ForEachBegin> get_foreach_begin() {
            //���ﲻ����m_foreach_begin_path����Ϊ���ܻ�û�ӻ�������ͬ��������������Ҫapply����ǰ�Ż�ͬ��
            std::string foreach_begin_path = zeno::reflect::any_cast<std::string>(get_defl_value("ForEachBegin Path"));
            std::shared_ptr<Graph> graph = this->getGraph().lock();
            std::shared_ptr<ForEachBegin> foreach_begin = std::dynamic_pointer_cast<ForEachBegin>(graph->getNode(foreach_begin_path));
            if (!foreach_begin) {
                throw makeError<KeyError>("foreach_begin_path", "the path of foreach_begin_path is not exist");
            }
            return foreach_begin;
        }

        ZENO_API void reset_forloop_settings() override {
            m_collect_objs->clear();
            std::shared_ptr<ForEachBegin> foreach_begin = get_foreach_begin();
            int start_value = zeno::reflect::any_cast<int>(get_defl_value("Start Value"));
            //ͦ�ɱ��ģ�������һ��m_start_value������Ϊui�޸ĵ�ʱ��û���ü�ͬ�����������ò���
            foreach_begin->update_iteration(/*m*/start_value);
        }

        ZENO_API bool is_continue_to_run() override {
            std::string iter_method = zeno::reflect::any_cast<std::string>(get_defl_value("Iterate Method"));

            std::shared_ptr<ForEachBegin> foreach_begin = get_foreach_begin();
            int current_iter = foreach_begin->get_current_iteration();

            if (iter_method == "By Count") {
                if (current_iter >= m_iterations) {
                    //TODO: stop conditon
                    return false;
                }
                return true;
            }
            else if (iter_method == "By Container") {
                zany initobj = foreach_begin->get_input("Initial Object");
                if (!initobj && foreach_begin->is_dirty()) {
                    //�������λ�û�㣬�Ȱ����ε���������
                    foreach_begin->preApply();
                    initobj = foreach_begin->get_input("Initial Object");
                }
                if (auto spList = std::dynamic_pointer_cast<ListObject>(initobj)) {
                    int n = spList->size();
                    return current_iter >= 0 && current_iter < n;
                }
                else {
                    return false;
                }
            }
            else {
                return false;
            }
        }

        ZENO_API void increment() override {
            if (m_iterate_method == "By Count" || m_iterate_method == "By Container") {
                std::shared_ptr<ForEachBegin> foreach_begin = get_foreach_begin();
                int current_iter = foreach_begin->get_current_iteration();
                int new_iter = current_iter + m_increment;
                foreach_begin->update_iteration(new_iter);
            }
            else {
                //TODO: By Container
            }
        }

        ZENO_API std::shared_ptr<IObject> get_iterate_object() override {
            return m_iterate_object;
        }

        std::shared_ptr<IObject> apply(std::shared_ptr<IObject> iterate_object) {
            //construct the `result` object
            m_iterate_object = iterate_object;
            if (m_iterate_method == "By Count" || m_iterate_method == "By Container") {
                if (m_collect_method == "Feedback to Begin") {
                    return m_iterate_object;
                }
                else if (m_collect_method == "Gather Each Iteration") {
                    zany new_obj = iterate_object->clone();
                    m_collect_objs->append(new_obj);
                    return m_collect_objs;
                }
                else {
                    assert(false);
                    return nullptr;
                }
            }
            else {
                throw makeError<UnimplError>("only support By Count or By Container at ForeachEnd");
            }
            return nullptr;
        }

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "ForEachBegin Path")
        std::string m_foreach_begin_path;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Iterations")
        int m_iterations = 10;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Increment")
        int m_increment = 1;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Start Value")
        int m_start_value = 0;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Stop Condition")
        int m_stop_condition = 1;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Iterate Method", Control = zeno::Combobox, ComboBoxItems = ("By Count", "By Container", "By Geometry Point", "By Geometry Face"))
        std::string m_iterate_method = "By Count";

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Collect Method", Control = zeno::Combobox, ComboBoxItems = ("Feedback to Begin", "Gather Each Iteration"))
        std::string m_collect_method = "Feedback to Begin";

        std::shared_ptr<IObject> m_iterate_object;
        std::shared_ptr<ListObject> m_collect_objs;     //TODO: ���foreach�Ķ�����Dict���������ռ��Ľ��������list���س�ȥ���Ժ���֧��Dict���ռ�
    };
}
