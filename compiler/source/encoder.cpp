#include "encoder.h"

#include <fstream>
#include <iostream>

#include "string_util.h"

void EncodeBuffer::push(const std::string& token)
{
	if (m_lines.empty())
	{
		m_lines.push_back(token);
	}
	else
	{
		m_lines[m_lines.size() - 1] += token;
	}
}

void EncodeBuffer::push_line(const std::string& line)
{
	m_lines.push_back(line);
}

std::string EncodeBuffer::string(const bool useEndline) const
{
	std::string content;
	for (const std::string& line : m_lines)
	{
		content += line;
		if (useEndline)
		{
			content += "\n";
		}
	}
	return content;
}

bool Encoder::encode(const SymbolList& symbolList, const TypeCollection& collection, const SymbolTable& symbolTable, const std::filesystem::path& path, const std::string& filename)
{
	static const auto read = [](const std::filesystem::path& filename) -> std::string
	{
		std::ostringstream buf;
		std::ifstream input(filename.c_str());
		buf << input.rdbuf();
		return buf.str();
	};

	std::filesystem::path outHeaderFilename = path / StringUtil::replace(filename, ".h", "_generated.h");
	std::filesystem::path outSourceFilename = path / StringUtil::replace(filename, ".h", "_generated.cpp");

	EncodeBuffer header_buffer;
	header_buffer.push_line("// Copyright (c) Vito Domenico Tagliente");
	header_buffer.push_line("// automatically generated by the compiler, do not modify");
	header_buffer.push_line("#pragma once");
	header_buffer.push_line("");
	header_buffer.push_line("#include <vdtreflect/runtime.h>");
	header_buffer.push_line("");

	EncodeBuffer source_buffer;
	source_buffer.push_line("// Copyright (c) Vito Domenico Tagliente");
	source_buffer.push_line("// automatically generated by the compiler, do not modify");
	source_buffer.push_line("#include \"", filename, "\"");
	source_buffer.push_line("");

	std::vector<TypeEnum*> enums;
	std::vector<TypeClass*> classes;

	for (const std::string& name : symbolList)
	{
		const auto& it = symbolTable.find(name);
		if (it == symbolTable.end())
		{
			std::cout << "Failed to find the symbol " << name << std::endl;
			return false;
		}

		SymbolType type = it->second;
		if (type == SymbolType::S_class)
		{
			TypeClass* const eClass = collection.findClass(name);
			if (eClass == nullptr)
			{
				std::cout << "Failed to find the class " << name << std::endl;
				return false;
			}
			classes.push_back(eClass);
		}
		else
		{
			TypeEnum* const eEnum = collection.findEnum(name);
			if (eEnum == nullptr)
			{
				std::cout << "Failed to find the enum " << name << std::endl;
				return false;
			}
			enums.push_back(eEnum);
		}
	}

	// enums
	for (TypeEnum* const eEnum : enums)
	{
		if (!encode(header_buffer, source_buffer, symbolTable, *eEnum))
		{
			std::cout << "Failed to encode the enum " << eEnum->name << std::endl;
			return false;
		}
	}

	// classes
	for (TypeClass* const eClass : classes)
	{
		if (!encode(header_buffer, source_buffer, symbolTable, collection, *eClass))
		{
			std::cout << "Failed to encode the class " << eClass->name << std::endl;
			return false;
		}
	}

	static const auto& generate = [](const EncodeBuffer& buffer, const std::string& filename) -> bool
	{
		std::string previousContent;
		std::string content = buffer.string();

		if (std::filesystem::exists(filename))
		{
			previousContent = read(filename);
		}

		if (previousContent != content)
		{
			std::ofstream fstream(filename);
			if (!fstream.is_open())
			{
				std::cout << "Failed to open the stream " << filename << std::endl;
				return false;
			}
			fstream << content;
			fstream.close();

			std::cout << filename << " generated" << std::endl;
		}
		return true;
	};

	// generate the header file only if it is changed or if it doesn't exist
	if (!generate(header_buffer, outHeaderFilename.string())) return false;
	// generate the source file only if it is changed or if it doesn't exist
	if (!generate(source_buffer, outSourceFilename.string())) return false;

	return true;
}

bool Encoder::encode(EncodeBuffer& headerBuffer, EncodeBuffer& sourceBuffer, const SymbolTable& symbolTable, const TypeCollection& collection, TypeClass& type)
{
	// header
	const std::string forward_keyword = type.isStruct ? "struct" : "class";
	headerBuffer.push_line("template <>");
	headerBuffer.push_line("struct reflect::Type<", forward_keyword, " ", type.name, "> : reflect::RegisteredInTypeFactory<", forward_keyword, " ", type.name, ">");
	headerBuffer.push_line("{");
	headerBuffer.push_line("    static const reflect::meta_t& meta();");
	headerBuffer.push_line("    static const char* const name();");
	headerBuffer.push_line("    static const reflect::properties_t& properties();");
	headerBuffer.push_line("");
	headerBuffer.push_line("    static bool registered() { return value; };");
	headerBuffer.push_line("};");
	headerBuffer.push_line("");

	// source
	sourceBuffer.push_line("const reflect::meta_t& reflect::Type<", type.name, ">::meta()");
	sourceBuffer.push_line("{");
	sourceBuffer.push_line("    static reflect::meta_t s_meta {");
	for (const auto& [key, value] : type.meta)
	{
		sourceBuffer.push_line("        { \"", key, "\", \"", value, "\" },");
	}
	sourceBuffer.push_line("    };");
	sourceBuffer.push_line("    return s_meta;");
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("const char* const reflect::Type<", type.name, ">::name() { return \"", type.name, "\"; }");
	sourceBuffer.push_line("");
	sourceBuffer.push_line("const reflect::properties_t& Type<", type.name, ">::properties()");
	sourceBuffer.push_line("{");
	sourceBuffer.push_line("    static reflect::properties_t s_properties {");
	// look for parent classes
	bool has_parent = false;
	std::string parent_name = type.parent;
	while (parent_name != "IType")
	{
		has_parent = true;
		TypeClass* const parentClass = collection.findClass(parent_name);
		if (parentClass == nullptr)
		{
			std::cout << "Cannot find the parent class " << parent_name << std::endl;
			return false;
		}

		sourceBuffer.push_line("        // Parent class ", parent_name, " properties");
		for (const Property& property : parentClass->properties)
		{
			sourceBuffer.push_line(encodePropertyReflection("        ", symbolTable, property, type.name) + ",");
		}

		parent_name = parentClass->parent;
	}
	if (has_parent)
	{
		sourceBuffer.push_line("        // Properties");
	}
	for (const Property& property : type.properties)
	{
		sourceBuffer.push_line(encodePropertyReflection("        ", symbolTable, property, type.name) + ",");
	}
	sourceBuffer.push_line("    };");
	sourceBuffer.push_line("    return s_properties;");
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("");
	sourceBuffer.push_line("const reflect::meta_t& ", type.name, "::type_meta() const { return reflect::Type<", type.name, ">::meta(); }");
	sourceBuffer.push_line("const char* const ", type.name, "::type_name() const { return reflect::Type<", type.name, ">::name(); }");
	sourceBuffer.push_line("const reflect::properties_t& ", type.name, "::type_properties() const { return reflect::Type<", type.name, ">::properties(); }");
	sourceBuffer.push_line("");
	sourceBuffer.push_line(type.name, "::operator std::string() const");
	sourceBuffer.push_line("{");
	sourceBuffer.push_line("    reflect::encoding::ByteBuffer buffer;");
	sourceBuffer.push_line("    reflect::encoding::OutputByteStream stream(buffer);");
	sourceBuffer.push_line("    stream << type_name();");
	sourceBuffer.push_line("    ");
	// look for parent classes
	has_parent = false;
	parent_name = type.parent;
	while (parent_name != "IType")
	{
		has_parent = true;
		TypeClass* const parentClass = collection.findClass(parent_name);
		if (parentClass == nullptr)
		{
			std::cout << "Cannot find the parent class " << parent_name << std::endl;
			return false;
		}

		sourceBuffer.push_line("    // Parent class ", parent_name, " properties");
		for (const Property& property : parentClass->properties)
		{
			const bool serialize = true;
			std::string temp = encodePropertySerialization("    ", symbolTable, serialize, property);
			if (!temp.empty())
				sourceBuffer.push_line(temp);
		}

		parent_name = parentClass->parent;
	}
	if (has_parent)
	{
		sourceBuffer.push_line("    // Properties");
	}
	for (const Property& property : type.properties)
	{
		const bool serialize = true;
		std::string temp = encodePropertySerialization("    ", symbolTable, serialize, property);
		if (!temp.empty())
			sourceBuffer.push_line(temp);
	}
	sourceBuffer.push_line("    ");
	sourceBuffer.push_line("    return std::string(reinterpret_cast<const char*>(&stream.getBuffer()[0]), stream.getBuffer().size());");
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("");
	sourceBuffer.push_line("void ", type.name, "::from_string(const std::string& str)");
	sourceBuffer.push_line("{");
	sourceBuffer.push_line("    reflect::encoding::ByteBuffer buffer;");
	sourceBuffer.push_line("    std::transform(");
	sourceBuffer.push_line("        std::begin(str),");
	sourceBuffer.push_line("        std::end(str),");
	sourceBuffer.push_line("        std::back_inserter(buffer),");
	sourceBuffer.push_line("        [](const char c)");
	sourceBuffer.push_line("        {");
	sourceBuffer.push_line("            return std::byte(c);");
	sourceBuffer.push_line("        }");
	sourceBuffer.push_line("    );");
	sourceBuffer.push_line("    ");
	sourceBuffer.push_line("    reflect::encoding::InputByteStream stream(buffer);");
	sourceBuffer.push_line("    std::string _name;");
	sourceBuffer.push_line("    stream >> _name;");
	sourceBuffer.push_line("    if (_name != type_name()) return;");
	sourceBuffer.push_line("    ");
	// look for parent classes
	has_parent = false;
	parent_name = type.parent;
	while (parent_name != "IType")
	{
		has_parent = true;
		TypeClass* const parentClass = collection.findClass(parent_name);
		if (parentClass == nullptr)
		{
			std::cout << "Cannot find the parent class " << parent_name << std::endl;
			return false;
		}

		sourceBuffer.push_line("    // Parent class ", parent_name, " properties");
		for (const Property& property : parentClass->properties)
		{
			const bool serialize = false;
			std::string temp = encodePropertySerialization("    ", symbolTable, serialize, property);
			if (!temp.empty())
				sourceBuffer.push_line(temp);
		}

		parent_name = parentClass->parent;
	}
	if (has_parent)
	{
		sourceBuffer.push_line("    // Properties");
	}
	for (const Property& property : type.properties)
	{
		const bool serialize = false;
		std::string temp = encodePropertySerialization("    ", symbolTable, serialize, property);
		if (!temp.empty())
			sourceBuffer.push_line(temp);
	}
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("");
	sourceBuffer.push_line("void ", type.name, "::from_json(const std::string& json)");
	sourceBuffer.push_line("{");
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("");
	sourceBuffer.push_line("std::string ", type.name, "::to_json() const");
	sourceBuffer.push_line("{");
	// look for parent classes
	has_parent = false;
	parent_name = type.parent;
	while (parent_name != "IType")
	{
		has_parent = true;
		TypeClass* const parentClass = collection.findClass(parent_name);
		if (parentClass == nullptr)
		{
			std::cout << "Cannot find the parent class " << parent_name << std::endl;
			return false;
		}

		sourceBuffer.push_line("    // Parent class ", parent_name, " properties");
		for (const Property& property : parentClass->properties)
		{
			const bool serialize = true;
			std::string temp = encodePropertySerializationToJson("    ", symbolTable, serialize, property);
			if (!temp.empty())
				sourceBuffer.push_line(temp);
		}

		parent_name = parentClass->parent;
	}
	if (has_parent)
	{
		sourceBuffer.push_line("    // Properties");
	}
	for (const Property& property : type.properties)
	{
		const bool serialize = true;
		std::string temp = encodePropertySerializationToJson("    ", symbolTable, serialize, property);
		if (!temp.empty())
			sourceBuffer.push_line(temp);
	}
	sourceBuffer.push_line("    return \"\";");
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("");

	return true;
}

bool Encoder::encode(EncodeBuffer& headerBuffer, EncodeBuffer& sourceBuffer, const SymbolTable& symbolTable, TypeEnum& type)
{
	// header
	headerBuffer.push_line("template <>");
	headerBuffer.push_line("struct reflect::Enum<enum class ", type.name, "> : reflect::RegisteredInEnumFactory<enum class ", type.name, ">");
	headerBuffer.push_line("{");
	headerBuffer.push_line("    static const char* const name();");
	headerBuffer.push_line("    static const reflect::enum_values_t& values();");
	headerBuffer.push_line("    ");
	headerBuffer.push_line("    static bool registered() { return value; };");
	headerBuffer.push_line("};");
	headerBuffer.push_line("");

	// source
	sourceBuffer.push_line("const char* const reflect::Enum<", type.name, ">::name() { return \"", type.name, "\"; }");
	sourceBuffer.push_line("const reflect::enum_values_t& reflect::Enum<", type.name, ">::values()");
	sourceBuffer.push_line("{");
	sourceBuffer.push_line("    static reflect::enum_values_t s_values{");
	for (const std::string& option : type.options)
	{
		sourceBuffer.push_line("        { \"", option, "\", static_cast<int>(", type.name, "::", option, ") }, ");
	}
	sourceBuffer.push_line("    };");
	sourceBuffer.push_line("    return s_values;");
	sourceBuffer.push_line("}");
	sourceBuffer.push_line("");

	return true;
}

std::string Encoder::encodePropertyReflection(const std::string& offset, const SymbolTable& symbolTable, const Property& property, const std::string& name)
{
	EncodeBuffer buffer;
	buffer.push(offset, "{ \"", property.name, "\", reflect::Property{ offsetof(", name, ", ", property.name, "), reflect::meta_t {");
	bool first = true;
	for (const auto& [key, value] : property.meta)
	{
		buffer.push(first ? "" : ", ", "{\"", key, "\", \"", value, "\"}");
		first = false;
	}
	buffer.push(" }, \"", property.name, "\", ", encodePropertyReflection(offset, symbolTable, property.type), " } }");
	return buffer.string(false);
}

std::string Encoder::encodePropertyReflection(const std::string& offset, const SymbolTable& symbolTable, const std::string& type)
{
	const DecoratorType decoratorType = parseDecoratorType(symbolTable, type);
	const NativeType nativeType = parseNativeType(symbolTable, type);
	std::vector<std::string> typenames;
	EncodeBuffer buffer;

	std::string off = offset + "    ";
	if (nativeType == NativeType::T_template)
	{
		typenames = extractTypenames(type);
		for (const std::string& tname : typenames)
		{
			buffer.push("\n" + off, encodePropertyReflection(offset + "    ", symbolTable, tname), ",");
		}
		buffer.push("\n" + offset);
	}
	else
	{
		buffer.push(" ");
	}

	return ("reflect::NativeType{ \"" + type + "\", { " + buffer.string(false) + "}, " + toString(decoratorType) + ", sizeof(" + type + "), " + toString(nativeType) + " }");
}

std::string Encoder::encodePropertySerialization(const std::string& offset, const SymbolTable& symbolTable, const bool serialize, const Property& property)
{
	return encodePropertySerialization(offset, symbolTable, serialize, property.name, property.type);
}

std::string Encoder::encodePropertySerialization(const std::string& offset, const SymbolTable& symbolTable, const bool serialize, const std::string& name, const std::string& type)
{
	const DecoratorType decoratorType = parseDecoratorType(symbolTable, type);
	const NativeType nativeType = parseNativeType(symbolTable, type);
	EncodeBuffer buffer;

	if (decoratorType != DecoratorType::D_raw) return "";

	switch (nativeType)
	{
	case NativeType::T_bool:
	case NativeType::T_char:
	case NativeType::T_double:
	case NativeType::T_float:
	case NativeType::T_int:
	case NativeType::T_string:
	{
		buffer.push(offset, "stream ", serialize ? "<<" : ">>", " ", name, ";");
		break;
	}
	case NativeType::T_enum:
	{
		if (serialize)
		{
			buffer.push(offset, "stream << static_cast<int>(", name, ");");
		}
		else
		{
			buffer.push(offset, "{");
			buffer.push("\n", offset, "    ", "int pack;");
			buffer.push("\n", offset, "    ", "stream >> pack;");
			buffer.push("\n", offset, "    ", name, " = static_cast<", type, ">(", name, ");");
			buffer.push("\n", offset, "}");
		}
		break;
	}
	case NativeType::T_template:
	{
		std::vector<std::string> typenames;
		typenames = extractTypenames(type);

		if (typenames.empty()) break;

		if (StringUtil::startsWith(type, "vector") || StringUtil::startsWith(type, "std::vector")
			|| StringUtil::startsWith(type, "list") || StringUtil::startsWith(type, "std::list"))
		{
			if (!isValidListType(parseNativeType(symbolTable, typenames[0]))) break;

			buffer.push(offset, "{");
			if (serialize)
			{
				buffer.push("\n", offset, "    ", "stream << ", name, ".size();");
				buffer.push("\n", offset, "    ", "for (const auto& element : ", name, ")");
			}
			else
			{
				buffer.push("\n", offset, "    ", "std::size_t size;");
				buffer.push("\n", offset, "    ", "stream >> size;");
				buffer.push("\n", offset, "    ", name, ".resize(size);");
				buffer.push("\n", offset, "    ", "for (int i = 0; i < ", name, ".size(); ++i)");
			}
			buffer.push("\n", offset, "    ", "{");
			if (serialize)
			{
				const std::string temp = encodePropertySerialization("    ", symbolTable, serialize, "element", typenames[0]);
				buffer.push("\n", offset, "    ", temp);
			}
			else
			{
				buffer.push("\n", offset, "        ", typenames[0], " element;");
				const std::string temp = encodePropertySerialization("", symbolTable, serialize, "element", typenames[0]);
				buffer.push("\n", offset, "        ", temp);
				buffer.push("\n", offset, "        ", name, ".push_back(element);");
			}
			buffer.push("\n", offset, "    ", "}");
			buffer.push("\n", offset, "}");
		}
		else if (StringUtil::startsWith(type, "map") || StringUtil::startsWith(type, "std::map")
			|| StringUtil::startsWith(type, "unordered_map") || StringUtil::startsWith(type, "std::unordered_map"))
		{
			if (typenames.size() < 2) break;
			if (!isValidMapKeyType(parseNativeType(symbolTable, typenames[0]))
				|| !isValidMapValueType(parseNativeType(symbolTable, typenames[1]))) break;

			buffer.push(offset, "{");
			if (serialize)
			{
				buffer.push("\n", offset, "    ", "stream << ", name, ".size();");
				buffer.push("\n", offset, "    ", "for (const auto& pair : ", name, ")");
				buffer.push("\n", offset, "    ", "{");
				std::string temp = encodePropertySerialization("    ", symbolTable, serialize, "pair.first", typenames[0]);
				buffer.push("\n", offset, "    ", temp);
				temp = encodePropertySerialization("    ", symbolTable, serialize, "pair.second", typenames[1]);
				buffer.push("\n", offset, "    ", temp);
				buffer.push("\n", offset, "    ", "}");
			}
			else
			{
				buffer.push("\n", offset, "    ", "std::size_t size;");
				buffer.push("\n", offset, "    ", "stream >> size;");
				buffer.push("\n", offset, "    ", "for (int i = 0; i < size; ++i)");
				buffer.push("\n", offset, "    ", "{");
				buffer.push("\n", offset, "        ", typenames[0], " key;");
				std::string temp = encodePropertySerialization("    ", symbolTable, serialize, "key", typenames[0]);
				buffer.push("\n", offset, "    ", temp);
				buffer.push("\n", offset, "        ", typenames[1], " value;");
				temp = encodePropertySerialization("    ", symbolTable, serialize, "value", typenames[1]);
				buffer.push("\n", offset, "    ", temp);
				buffer.push("\n", offset, "        ", name, ".insert(std::make_pair(key, value));");
				buffer.push("\n", offset, "    ", "}");
			}
			buffer.push("\n", offset, "}");
		}
		break;
	}
	case NativeType::T_type:
	{
		if (serialize)
		{
			buffer.push(offset, "stream << static_cast<std::string>(", name, ");");
		}
		else
		{
			buffer.push(offset, "{");
			buffer.push("\n", offset, "    ", "std::string pack;");
			buffer.push("\n", offset, "    ", "stream >> pack;");
			buffer.push("\n", offset, "    ", name, ".from_string(pack);");
			buffer.push("\n", offset, "}");
		}
		break;
	}
	default:
	case NativeType::T_void:
	case NativeType::T_unknown:
		break;
	}

	return buffer.string(false);
}

std::string Encoder::encodePropertySerializationToJson(const std::string& offset, const SymbolTable& symbolTable, const bool serialize, const Property& property)
{
	return "";
}

NativeType Encoder::parseNativeType(const SymbolTable& symbolTable, const std::string& t)
{
	std::string type = StringUtil::replace(t, "std::", "");
	if (type.empty()) return NativeType::T_unknown;

	while (!type.empty() && (type[type.length() - 1] == '*' || type[type.length() - 1] == '&' || type[type.length() - 1] == ' '))
	{
		type.pop_back();
	}

	if (type == "bool") return NativeType::T_bool;
	if (type == "char") return NativeType::T_char;
	if (type == "double") return NativeType::T_double;
	if (type == "float") return NativeType::T_float;
	if (type == "int")  return NativeType::T_int;
	if (type == "void")  return NativeType::T_void;
	if (type == "string") return NativeType::T_string;
	if (type.find("<") != std::string::npos) return NativeType::T_template;

	const auto& it = symbolTable.find(type);
	if (it != symbolTable.end())
	{
		switch (it->second)
		{
		case SymbolType::S_class: return NativeType::T_type;
		case SymbolType::S_enum: return NativeType::T_enum;
		default: return NativeType::T_unknown;
		}
	}

	return NativeType::T_unknown;
}

DecoratorType Encoder::parseDecoratorType(const SymbolTable& symbolTable, const std::string& t)
{
	if (StringUtil::endsWith(t, "*")) return DecoratorType::D_pointer;
	if (StringUtil::endsWith(t, "&")) return DecoratorType::D_reference;
	return DecoratorType::D_raw;
}

std::vector<std::string> Encoder::extractTypenames(const std::string& token)
{
	std::vector<std::string> typenames;
	const size_t startIndex = token.find('<');
	if (startIndex == std::string::npos) return typenames;

	if (token.back() != '>') return typenames;

	std::string content = token.substr(startIndex + 1, token.length());
	content.pop_back();

	for (std::string t : StringUtil::split(content, ','))
	{
		typenames.push_back(StringUtil::trim(t));
	}
	return typenames;
}

std::string Encoder::sanitizeTemplate(const std::string& token)
{
	const size_t startIndex = token.find('<');
	if (startIndex == std::string::npos) return token;

	if (token.back() != '>') return token;

	std::string content = token.substr(startIndex + 1, token.length());
	content.pop_back();

	return content;
}

std::string Encoder::toString(const NativeType type)
{
	switch (type)
	{
	case NativeType::T_bool: return "reflect::NativeType::Type::T_bool";
	case NativeType::T_char: return "reflect::NativeType::Type::T_char";
	case NativeType::T_double: return "reflect::NativeType::Type::T_double";
	case NativeType::T_enum: return "reflect::NativeType::Type::T_enum";
	case NativeType::T_float: return "reflect::NativeType::Type::T_float";
	case NativeType::T_int: return "reflect::NativeType::Type::T_int";
	case NativeType::T_string: return "reflect::NativeType::Type::T_string";
	case NativeType::T_template: return "reflect::NativeType::Type::T_template";
	case NativeType::T_type: return "reflect::NativeType::Type::T_type";
	case NativeType::T_void: return "reflect::NativeType::Type::T_void";
	default:
	case NativeType::T_unknown: return "reflect::NativeType::Type::T_unknown";
	}
}

std::string Encoder::toString(const DecoratorType type)
{
	switch (type)
	{
	case DecoratorType::D_pointer: return "reflect::NativeType::DecoratorType::D_pointer";
	case DecoratorType::D_reference: return "reflect::NativeType::DecoratorType::D_reference";
	default:
	case DecoratorType::D_raw: return "reflect::NativeType::DecoratorType::D_raw";
	}
}

bool Encoder::isValidListType(const NativeType type)
{
	return type != NativeType::T_template
		&& type != NativeType::T_void
		&& type != NativeType::T_unknown;
}

bool Encoder::isValidMapKeyType(const NativeType type)
{
	return type != NativeType::T_template
		&& type != NativeType::T_void
		&& type != NativeType::T_unknown;
}

bool Encoder::isValidMapValueType(const NativeType type)
{
	return type != NativeType::T_template
		&& type != NativeType::T_void
		&& type != NativeType::T_unknown;
}
