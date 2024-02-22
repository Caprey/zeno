
if (EXISTS ${OUTPUT})
	foreach(dir ${OBJECTS})
		if (EXISTS ${dir})
            cmake_path(GET dir STEM folder)
			execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${dir} ${OUTPUT}/${folder})
			message("copied dir [${dir}] to [${OUTPUT}/${folder}]")
		endif()
	endforeach()
endif()