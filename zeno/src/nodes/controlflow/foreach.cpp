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

        std::shared_ptr<IObject> apply(std::shared_ptr<IObject> init_object) {
            _out_iteration = m_current_iteration;
            if (m_fetch_mehod == "Initial Object") {
                return init_object;
            }
            else if (m_fetch_mehod == "From Last Feedback") {
                std::shared_ptr<Graph> graph = this->getGraph().lock();
                std::shared_ptr<INode> foreach_end = graph->getNode(m_foreach_end_path);
                if (!foreach_end) {
                    throw makeError<KeyError>("foreach_end_path", "the path of foreach_end_path is not exist");
                }
                int startValue = zeno::reflect::any_cast<int>(foreach_end->get_defl_value("Start Value"));
                if (startValue == m_current_iteration) {
                    return init_object;
                }
                else {
                    std::shared_ptr<IObject> outputObj = foreach_end->get_output_obj("Output Object");
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
            update_param_impl("Current Iteration", m_current_iteration);
        }

    //private:
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

        std::shared_ptr<ForEachBegin> get_foreach_begin() {
            //���ﲻ����m_foreach_begin_path����Ϊ���ܻ�û�ӻ�������ͬ��������������Ҫapply����ǰ�Ż�ͬ��
            std::string foreach_begin_path = zeno::reflect::any_cast<std::string>(get_defl_value("ForEachBegin Path"));
            std::shared_ptr<Graph> graph = this->getGraph().lock();
            std::shared_ptr<ForEachBegin> foreach_begin = std::dynamic_pointer_cast<ForEachBegin>(graph->getNode(foreach_begin_path));
            return foreach_begin;
        }

        ZENO_API bool is_continue_to_run() override {
            std::shared_ptr<Graph> graph = this->getGraph().lock();
            std::shared_ptr<ForEachBegin> foreach_begin = get_foreach_begin();
            if (!foreach_begin) {
                throw makeError<KeyError>("foreach_begin_path", "the path of foreach_begin_path is not exist");
            }

            if (foreach_begin->get_current_iteration() >= m_iterations) {
                //TODO: stop conditon
                return false;
            }
            return true;
        }

        ZENO_API void increment() override {
            if (m_iterate_method == "By Count") {
                std::shared_ptr<Graph> graph = this->getGraph().lock();
                std::shared_ptr<ForEachBegin> foreach_begin = get_foreach_begin();
                int current_iter = foreach_begin->get_current_iteration();
                int new_iter = current_iter + m_increment;
                foreach_begin->update_iteration(new_iter);
            }
            else {
                //TODO: by object container
            }
        }

        std::shared_ptr<IObject> apply(std::shared_ptr<IObject> iterate_object)
        {
            std::shared_ptr<IObject> res;

            std::shared_ptr<Graph> graph = this->getGraph().lock();
            std::shared_ptr<ForEachBegin> foreach_begin = get_foreach_begin();
            if (!foreach_begin) {
                throw makeError<KeyError>("foreach_begin_path", "the path of foreach_begin_path is not exist");
            }

            int curr_iter = foreach_begin->get_current_iteration();
            if (curr_iter >= m_iterations) {
                //TODO: stop_condition
                return iterate_object;
            }

            //construct the `result` object
            if (m_iterate_method == "By Count") {
                if (m_collect_method == "Feedback to Begin") {
                    return iterate_object;
                }
                else if (m_collect_method == "Gather Each Iteration") {
                    //TODO: list
                }
                else {
                    assert(false);
                    return nullptr;
                }
            }
            else {
                //by object container
            }
            return nullptr;
        }

    //private:
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

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Iterate Method", Control = zeno::Combobox, ComboBoxItems = ("By Count", "By Object Container"))
        std::string m_iterate_method = "By Count";

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Collect Method", Control = zeno::Combobox, ComboBoxItems = ("Feedback to Begin", "Gather Each Iteration"))
        std::string m_collect_method = "Feedback to Begin";
    };
}
