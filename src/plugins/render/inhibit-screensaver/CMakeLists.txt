OPTION(BUILD_INHIBIT_SCREENSAVER_PLUGIN "Build the Maemo specific inhibit-screensaver plugin")

IF( BUILD_INHIBIT_SCREENSAVER_PLUGIN )
  PROJECT(InhibitScreensaverPlugin)

  INCLUDE_DIRECTORIES(
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${CMAKE_CURRENT_BINARY_DIR}
   ${QT_INCLUDE_DIR}
   ${QT_QTNETWORK_INCLUDE_DIR}
  )

  if( QT4_FOUND )
    INCLUDE(${QT_USE_FILE})
  endif()

  set( inhibit-screensaver_SRCS InhibitScreensaverPlugin.cpp )		 

  marble_add_plugin( InhibitScreensaverPlugin ${inhibit-screensaver_SRCS} )
ENDIF()
