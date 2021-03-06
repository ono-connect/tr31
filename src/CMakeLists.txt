##############################################################################
# Copyright (c) 2020, 2021 ono//connect
#
# This file is licensed under the terms of the LGPL v2.1 license.
# See LICENSE file.
##############################################################################

cmake_minimum_required(VERSION 3.16)

find_package(MbedTLS 2.16) # Latest version available on Ubuntu 20.04 and Fedora 34
option(FETCH_MBEDTLS "Download and build MbedTLS")
if(NOT MbedTLS_FOUND AND FETCH_MBEDTLS)
	include(FetchMbedTLS)
endif()

find_package(OpenSSL 1.1 COMPONENTS Crypto)

# choose crypto implementation and inform parent scope
if(NOT MbedTLS_FOUND AND NOT OpenSSL_FOUND)
	message(FATAL_ERROR "Either MbedTLS or OpenSSL is required")
endif()
if(MbedTLS_FOUND)
	# prefer MbedTLS
	message(STATUS "Using MbedTLS")
	set(USE_MBEDTLS TRUE)
	list(APPEND TR31_PACKAGE_DEPENDENCIES "MbedTLS 2.16")
	set(TR31_PACKAGE_DEPENDENCIES ${TR31_PACKAGE_DEPENDENCIES} PARENT_SCOPE)
	# NOTE: MbedTLS has no pkgconfig file so TR31_PKGCONFIG_REQ_PRIV cannot be set
	set(TR31_PKGCONFIG_LIBS_PRIV "-lmbedcrypto" PARENT_SCOPE)
else()
	message(STATUS "Using OpenSSL")
	set(USE_OPENSSL TRUE)
	list(APPEND TR31_PACKAGE_DEPENDENCIES "OpenSSL 1.1 COMPONENTS Crypto")
	set(TR31_PACKAGE_DEPENDENCIES ${TR31_PACKAGE_DEPENDENCIES} PARENT_SCOPE)
	set(TR31_PKGCONFIG_REQ_PRIV "libcrypto" PARENT_SCOPE)
	set(TR31_PKGCONFIG_LIBS_PRIV "-lcrypto" PARENT_SCOPE)
endif()

include(CheckFunctionExists)
check_function_exists("argp_parse" argp_FOUND)
option(FETCH_ARGP "Download and build argp-standalone")
if(NOT argp_FOUND)
	if(FETCH_ARGP)
		set(ARGP_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
		include(FetchArgpStandalone)
	else()
		message(FATAL_ERROR "Could NOT find argp on target system. Enable FETCH_ARGP to download and build argp-standalone.")
	endif()
endif()

include(GNUInstallDirs) # provides CMAKE_INSTALL_* variables and good defaults for install()

# generate config file for internal use only
# this file should NOT be installed or used by an installed header
configure_file(
	tr31_config.h.in
	tr31_config.h
)

add_library(tr31 tr31.c tr31_crypto.c)
set_target_properties(tr31
	PROPERTIES
		PUBLIC_HEADER tr31.h
		VERSION ${CMAKE_PROJECT_VERSION}
		SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}
)
target_include_directories(tr31 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}) # for generated config file
target_include_directories(tr31 INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)
if(MbedTLS_FOUND)
	target_link_libraries(tr31 MbedTLS::mbedcrypto)
elseif(OpenSSL_FOUND)
	target_link_libraries(tr31 OpenSSL::Crypto)
endif()
install(TARGETS tr31
	EXPORT tr31Targets # for use by install(EXPORT) command
	PUBLIC_HEADER
		DESTINATION "include/${PROJECT_NAME}"
		COMPONENT tr31_development
	RUNTIME
		COMPONENT tr31_runtime
	LIBRARY
		COMPONENT tr31_runtime
		NAMELINK_COMPONENT tr31_development
	ARCHIVE
		COMPONENT tr31_development
)

add_executable(tr31-tool tr31-tool.c)
target_link_libraries(tr31-tool tr31)
if(TARGET argp::argp)
	target_link_libraries(tr31-tool argp::argp)
endif()
install(TARGETS tr31-tool
	RUNTIME
		COMPONENT tr31_runtime
)

if(BUILD_TESTING)
	add_test(NAME tr31_tool_test1
		COMMAND tr31-tool --import B0128B1TX00N0300KS18FFFF00A0200001E00000KC0C000169E3KP0C00ECAD626F9F1A826814AA066D86C8C18BD0E14033E1EBEC75BEDF586E6E325F3AA8C0E5 --kbpk AB2E09DB3EF0BA71E0CE6CD755C23A3B
	)
	set_tests_properties(tr31_tool_test1
		PROPERTIES
			PASS_REGULAR_EXPRESSION "Key value: BF82DAC6A33DF92CE66E15B70E5DCEB6 \\(KCV: 0169E3\\)"
	)

	add_test(NAME tr31_tool_test2
		COMMAND tr31-tool --import D0112B0TN00N000037DB9B046B7B0048785690759580ABC3B9842AB4BB7717B49E92528E575785D8123559376A2553B27BE94F054F4E971C --kbpk 4141414141414141414141414141414141414141414141414141414141414141
	)
	set_tests_properties(tr31_tool_test2
		PROPERTIES
			PASS_REGULAR_EXPRESSION "Key value: 1FA1F7CEC798D91545DA8AE0C7796BD9 \\(KCV: FF5087\\)"
	)

	add_test(NAME tr31_tool_test3
		COMMAND tr31-tool --import D0144B0AN00N0000DCD6D7E8108277E3304831A744F741A51A30695A2F764C6E42A1F54086FF0C3F5F9BEC889F20C6F613EF790A09381A5855B9464E598CBE24E537B6FE0F602297 --kbpk 4141414141414141414141414141414141414141414141414141414141414141
	)
	set_tests_properties(tr31_tool_test3
		PROPERTIES
			PASS_REGULAR_EXPRESSION "Key value: 6189A1111AAB43DC8BEAEEAE29799C4212070D66F8933EFACE8879C7DB609DEA \\(KCV: 69EBA40B13\\)"
	)

	add_test(NAME tr31_tool_test4
		COMMAND tr31-tool --import D0144B0AN00N000004E08A6000D7B21A5CFFE0F24E449A21D9BE3F975D33DB02D50CF9A6CCD2F6B6DB375CBCCCA648E0FB991432CA3957E810B087CDA7FA44538CC94C97FFF6D696 --kbpk 4141414141414141414141414141414141414141414141414141414141414141
	)
	set_tests_properties(tr31_tool_test4
		PROPERTIES
			PASS_REGULAR_EXPRESSION "Key value: 6189A1111AAB43DC8BEAEEAE29799C4212070D66F8933EFACE8879C7DB609DEA \\(KCV: 69EBA40B13\\)"
	)

	add_test(NAME tr31_tool_test5
		COMMAND tr31-tool --import D0144D0AN00N0000127862F945C2DED04530FAF7CDBC8B0BA10C7AA79BD5E0C2C5D6AC173BF588E4B19ACF1357178D50EA0AB193228E13958304FC6149632DFDCADF3A5B3D57E814 --kbpk 4141414141414141414141414141414141414141414141414141414141414141
	)
	set_tests_properties(tr31_tool_test5
		PROPERTIES
			PASS_REGULAR_EXPRESSION "Key value: BE19E6A07A760F10EF8E83A226B63AAD141F463FDDD4F47DB244B4023EC3CACC \\(KCV: 0A00E31EEB\\)"
	)

	add_test(NAME tr31_tool_test6
		COMMAND tr31-tool --kbpk 1D22BF32387C600AD97F9B97A51311AC --export E8BC63E5479455E26577F715D587FE68 --export-key-algorithm TDES --export-format-version B --export-template IK
	)
	set_tests_properties(tr31_tool_test6
		PROPERTIES
			WILL_FAIL TRUE # because --export-opt-block-KS is absent
	)

	add_test(NAME tr31_tool_test7
		COMMAND tr31-tool --kbpk B8ED59E0A279A295E9F5ED7944FD06B9 --export EDB380DD340BC2620247D445F5B8D678 --export-key-algorithm TDES --export-format-version C --export-template BDK
	)
	set_tests_properties(tr31_tool_test7
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^C0072B0TX00E0000"
	)

	add_test(NAME tr31_tool_test8
		COMMAND tr31-tool --kbpk 88E1AB2A2E3DD38C1FA039A536500CC8A87AB9D62DC92C01058FA79F44657DE6 --export 3F419E1CB7079442AA37474C2EFBF8B8 --export-key-algorithm AES --export-format-version D --export-template KEK
	)
	set_tests_properties(tr31_tool_test8
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^D0112K0AB00E0000"
	)

	add_test(NAME tr31_tool_test9
		COMMAND tr31-tool --kbpk 1D22BF32387C600AD97F9B97A51311AC --export E8BC63E5479455E26577F715D587FE68 --export-header B0080B1TX00N0000
	)
	set_tests_properties(tr31_tool_test9
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^B0080B1TX00N0000"
	)

	add_test(NAME tr31_tool_test10
		COMMAND tr31-tool --kbpk B8ED59E0A279A295E9F5ED7944FD06B9 --export EDB380DD340BC2620247D445F5B8D678 --export-header C0072B0TX00E0000
	)
	set_tests_properties(tr31_tool_test10
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^C0072B0TX00E0000"
	)

	add_test(NAME tr31_tool_test11
		COMMAND tr31-tool --kbpk 88E1AB2A2E3DD38C1FA039A536500CC8A87AB9D62DC92C01058FA79F44657DE6 --export 3F419E1CB7079442AA37474C2EFBF8B8 --export-header D0112K0AB00E0000
	)
	set_tests_properties(tr31_tool_test11
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^D0112K0AB00E0000"
	)

	add_test(NAME tr31_tool_test12
		COMMAND tr31-tool --kbpk 1D22BF32387C600AD97F9B97A51311AC --export E8BC63E5479455E26577F715D587FE68 --export-key-algorithm TDES --export-format-version B --export-template IK --export-opt-block-KS FFFF00A0200001E00000
	)
	set_tests_properties(tr31_tool_test12
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^B0104B1TX00N0100KS18FFFF00A0200001E00000"
	)

	add_test(NAME tr31_tool_test13
		COMMAND tr31-tool --kbpk 1D22BF32387C600AD97F9B97A51311AC --export E8BC63E5479455E26577F715D587FE68 --export-header B0000B1TX00N0000 --export-opt-block-KS FFFF00A0200001E00000
	)
	set_tests_properties(tr31_tool_test13
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^B0104B1TX00N0100KS18FFFF00A0200001E00000"
	)

	add_test(NAME tr31_tool_test14
		COMMAND tr31-tool --kbpk B8ED59E0A279A295E9F5ED7944FD06B9 --export EDB380DD340BC2620247D445F5B8D678 --export-key-algorithm TDES --export-format-version C --export-template BDK --export-opt-block-KC --export-opt-block-KP
	)
	set_tests_properties(tr31_tool_test14
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^C0096B0TX00E0200KC0C00F4B08DKP0C002F0EEA"
	)

	add_test(NAME tr31_tool_test15
		COMMAND tr31-tool --kbpk 88E1AB2A2E3DD38C1FA039A536500CC8A87AB9D62DC92C01058FA79F44657DE6 --export 3F419E1CB7079442AA37474C2EFBF8B8 --export-key-algorithm AES --export-format-version D --export-template KEK --export-opt-block-KC --export-opt-block-KP
	)
	set_tests_properties(tr31_tool_test15
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^D0144K0AB00E0200KC100108793E25ABKP10012331550BC9"
	)

	add_test(NAME tr31_tool_test16
		COMMAND tr31-tool --kbpk 4141414141414141414141414141414141414141414141414141414141414141 --export 1273671EA26AC29AFA4D1084127652A1 --export-key-algorithm AES --export-format-version D --export-template IK --export-opt-block-IK 1234567890123456
	)
	set_tests_properties(tr31_tool_test16
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^D0144B1AX00N0200IK141234567890123456PB0C00000000"
	)

	add_test(NAME tr31_tool_test17
		COMMAND tr31-tool --kbpk 4141414141414141414141414141414141414141414141414141414141414141 --export 1273671EA26AC29AFA4D1084127652A1 --export-header D0000B1AX00N0000 --export-opt-block-IK 1234567890123456
	)
	set_tests_properties(tr31_tool_test17
		PROPERTIES
			PASS_REGULAR_EXPRESSION "^D0144B1AX00N0200IK141234567890123456PB0C00000000"
	)
endif()
