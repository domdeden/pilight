file(GLOB functions "${PROJECT_SOURCE_DIR}/functions/*.lua")
if(WIN32)
	install(FILES ${functions} DESTINATION functions/ COMPONENT pilight)
else()
	install(FILES ${functions} DESTINATION lib/pilight/functions/ COMPONENT pilight)
endif()
