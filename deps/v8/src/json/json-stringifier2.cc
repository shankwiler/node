// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json/json-stringifier2.h"

#include "src/common/message-template.h"
#include "src/numbers/conversions.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/smi.h"
#include "src/strings/string-builder-inl.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class JSONStringifier2 {
 public:
  explicit JSONStringifier2(Isolate* isolate);

  ~JSONStringifier2() { DeleteArray(gap_); }

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Stringify(Handle<Object> object,
                                                      Handle<Object> replacer,
                                                      Handle<Object> gap);

 private:
  public: enum Result { UNCHANGED, SUCCESS, EXCEPTION }; private:

  bool InitializeReplacer(Handle<Object> replacer);
  bool InitializeGap(Handle<Object> gap);

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> ApplyToJsonFunction(
      Handle<Object> object, Handle<Object> key);
  V8_WARN_UNUSED_RESULT MaybeHandle<Object> ApplyReplacerFunction(
      Handle<Object> value, Handle<Object> key, Handle<Object> initial_holder);

  // Entry point to serialize the object.
  V8_INLINE Result SerializeObject(Handle<Object> obj) {
    return Serialize_<false>(obj, false, factory()->empty_string());
  }

  // Serialize an array element.
  // The index may serve as argument for the toJSON function.
  public: V8_INLINE Result SerializeElement(Isolate* isolate, Handle<Object> object,
                                    int i) {
    return Serialize_<false>(object, false,
                             Handle<Object>(Smi::FromInt(i), isolate));
  } private:

  // Serialize a object property.
  // The key may or may not be serialized depending on the property.
  // The key may also serve as argument for the toJSON function.
  V8_INLINE Result SerializeProperty(Handle<Object> object, bool deferred_comma,
                                     Handle<String> deferred_key) {
    DCHECK(!deferred_key.is_null());
    return Serialize_<true>(object, deferred_comma, deferred_key);
  }

  template <bool deferred_string_key>
  Result Serialize_(Handle<Object> object, bool comma, Handle<Object> key);

  V8_INLINE void SerializeDeferredKey(bool deferred_comma,
                                      Handle<Object> deferred_key);

  Result SerializeSmi(Smi object);

  Result SerializeDouble(double number);
  V8_INLINE Result SerializeHeapNumber(Handle<HeapNumber> object) {
    return SerializeDouble(object->value());
  }

  Result SerializeJSPrimitiveWrapper(Handle<JSPrimitiveWrapper> object,
                                     Handle<Object> key);

  public: V8_INLINE Result SerializeJSArray(Handle<JSArray> object, Handle<Object> key); private:
  V8_INLINE Result SerializeJSObject(Handle<JSObject> object,
                                     Handle<Object> key);

  Result SerializeJSProxy(Handle<JSProxy> object, Handle<Object> key);
  Result SerializeJSReceiverSlow(Handle<JSReceiver> object);
  Result SerializeArrayLikeSlow(Handle<JSReceiver> object, uint32_t start,
                                uint32_t length);

  void SerializeString(Handle<String> object);

  template <typename SrcChar, typename DestChar>
  V8_INLINE static void SerializeStringUnchecked_(
      Vector<const SrcChar> src,
      IncrementalStringBuilder::NoExtend<DestChar>* dest);

  template <typename SrcChar, typename DestChar>
  V8_INLINE void SerializeString_(Handle<String> string);

  template <typename Char>
  V8_INLINE static bool DoNotEscape(Char c);

  V8_INLINE void NewLine();
  V8_INLINE void Indent() { indent_++; }
  V8_INLINE void Unindent() { indent_--; }
  V8_INLINE void Separator(bool first);

  Handle<JSReceiver> CurrentHolder(Handle<Object> value,
                                   Handle<Object> inital_holder);

  Result StackPush(Handle<Object> object, Handle<Object> key);
  void StackPop();

  // Uses the current stack_ to provide a detailed error message of
  // the objects involved in the circular structure.
  Handle<String> ConstructCircularStructureErrorMessage(Handle<Object> last_key,
                                                        size_t start_index);
  // The prefix and postfix count do NOT include the starting and
  // closing lines of the error message.
  static const int kCircularErrorMessagePrefixCount = 2;
  static const int kCircularErrorMessagePostfixCount = 1;

  public: Factory* factory() { return isolate_->factory(); } private:

  public: Isolate* isolate_; private:
  public: IncrementalStringBuilder builder_; private:
  Handle<String> tojson_string_;
  Handle<FixedArray> property_list_;
  Handle<JSReceiver> replacer_function_;
  uc16* gap_;
  int indent_;

  using KeyObject = std::pair<Handle<Object>, Handle<Object>>;
  std::vector<KeyObject> stack_;

  static const int kJsonEscapeTableEntrySize = 8;
  static const char* const JsonEscapeTable;
};

struct JsonContinuation {
  enum Type : uint8_t { kReturn, kObjectProperty, kArrayElement };
  JsonContinuation(Isolate* isolate, Type type, size_t index)
      : scope(isolate),
        type_(type),
        index(static_cast<uint32_t>(index)),
        max_index(0),
        elements(0) {}

  Type type() const { return static_cast<Type>(type_); }
  void set_type(Type type) { type_ = static_cast<uint8_t>(type); }

  HandleScope scope;
  // Unfortunately GCC doesn't like packing Type in two bits.
  uint32_t type_ : 2;
  uint32_t index : 30;
  uint32_t max_index;
  uint32_t elements;
};

/*MaybeHandle<Object> JsonStringify2Old(Isolate* isolate, Handle<Object> object,
                                  Handle<Object> replacer, Handle<Object> gap) {
  //.
  /* One true value std::vector<JsonContinuation> cont_stack;

  cont_stack.reserve(16);

  JsonContinuation cont(isolate, JsonContinuation::kReturn, 0);

  Handle<Object> value;
  JSONStringifier2 stringifier(isolate);//used only for the factory method
  value = stringifier.factory()->true_value();
  return cont.scope.CloseAndEscape(value); END COMMENT
    
}*/

class JsonString final {
 public:
  JsonString()
      : start_(0),
        length_(0),
        needs_conversion_(false),
        internalize_(false),
        has_escape_(false),
        is_index_(false) {}

  explicit JsonString(uint32_t index)
      : index_(index),
        length_(0),
        needs_conversion_(false),
        internalize_(false),
        has_escape_(false),
        is_index_(true) {}

  JsonString(int start, int length, bool needs_conversion,
             bool needs_internalization, bool has_escape)
      : start_(start),
        length_(length),
        needs_conversion_(needs_conversion),
        internalize_(needs_internalization ||
                     length_ <= kMaxInternalizedStringValueLength),
        has_escape_(has_escape),
        is_index_(false) {}

  bool internalize() const {
    DCHECK(!is_index_);
    return internalize_;
  }

  bool needs_conversion() const {
    DCHECK(!is_index_);
    return needs_conversion_;
  }

  bool has_escape() const {
    DCHECK(!is_index_);
    return has_escape_;
  }

  int start() const {
    DCHECK(!is_index_);
    return start_;
  }

  int length() const {
    DCHECK(!is_index_);
    return length_;
  }

  uint32_t index() const {
    DCHECK(is_index_);
    return index_;
  }

  bool is_index() const { return is_index_; }

 private:
  static const int kMaxInternalizedStringValueLength = 10;

  union {
    const int start_;
    const uint32_t index_;
  };
  const int length_;
  const bool needs_conversion_ : 1;
  const bool internalize_ : 1;
  const bool has_escape_ : 1;
  const bool is_index_ : 1;
};

struct JsonContinuation {
  enum Type : uint8_t { kReturn, kObjectProperty, kArrayElement };
  JsonContinuation(Isolate* isolate, Type type, size_t index)
      : scope(isolate),
        type_(type),
        index(static_cast<uint32_t>(index)),
        max_index(0),
        elements(0) {}

  Type type() const { return static_cast<Type>(type_); }
  void set_type(Type type) { type_ = static_cast<uint8_t>(type); }

  HandleScope scope;
  // Unfortunately GCC doesn't like packing Type in two bits.
  uint32_t type_ : 2;
  uint32_t index : 30;
  uint32_t max_index;
  uint32_t elements;
};

struct JsonProperty {
  JsonProperty() { UNREACHABLE(); }
  explicit JsonProperty(const JsonString& string) : string(string) {}

  JsonString string;
  Handle<Object> value;
};

template <typename Char>
Handle<Object> BuildJsonObject(
    const JsonContinuation& cont,
    const std::vector<JsonProperty>& property_stack, Handle<Map> feedback) {
  size_t start = cont.index;
  int length = static_cast<int>(property_stack.size() - start);
  int named_length = length - cont.elements;

  Handle<Map> initial_map = factory()->ObjectLiteralMapFromCache(
      isolate_->native_context(), named_length);

  Handle<Map> map = initial_map;

  Handle<FixedArrayBase> elements = factory()->empty_fixed_array();

  // First store the elements.
  if (cont.elements > 0) {
    // Store as dictionary elements if that would use less memory.
    if (ShouldConvertToSlowElements(cont.elements, cont.max_index + 1)) {
      Handle<NumberDictionary> elms =
          NumberDictionary::New(isolate_, cont.elements);
      for (int i = 0; i < length; i++) {
        const JsonProperty& property = property_stack[start + i];
        if (!property.string.is_index()) continue;
        uint32_t index = property.string.index();
        Handle<Object> value = property.value;
        elms = NumberDictionary::Set(isolate_, elms, index, value);
      }
      map = Map::AsElementsKind(isolate_, map, DICTIONARY_ELEMENTS);
      elements = elms;
    } else {
      Handle<FixedArray> elms =
          factory()->NewFixedArrayWithHoles(cont.max_index + 1);
      DisallowHeapAllocation no_gc;
      WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);
      DCHECK_EQ(HOLEY_ELEMENTS, map->elements_kind());

      for (int i = 0; i < length; i++) {
        const JsonProperty& property = property_stack[start + i];
        if (!property.string.is_index()) continue;
        uint32_t index = property.string.index();
        Handle<Object> value = property.value;
        elms->set(static_cast<int>(index), *value, mode);
      }
      elements = elms;
    }
  }

  int feedback_descriptors =
      (feedback.is_null() ||
       feedback->elements_kind() != map->elements_kind() ||
       feedback->instance_size() != map->instance_size())
          ? 0
          : feedback->NumberOfOwnDescriptors();

  int i;
  int descriptor = 0;
  int new_mutable_double = 0;
  for (i = 0; i < length; i++) {
    const JsonProperty& property = property_stack[start + i];
    if (property.string.is_index()) continue;
    Handle<String> expected;
    Handle<Map> target;
    if (descriptor < feedback_descriptors) {
      expected = handle(
          String::cast(feedback->instance_descriptors().GetKey(descriptor)),
          isolate_);
    } else {
      DisallowHeapAllocation no_gc;
      TransitionsAccessor transitions(isolate(), *map, &no_gc);
      expected = transitions.ExpectedTransitionKey();
      if (!expected.is_null()) {
        // Directly read out the target while reading out the key, otherwise it
        // might die while building the string below.
        target = TransitionsAccessor(isolate(), *map, &no_gc)
                     .ExpectedTransitionTarget();
      }
    }

    Handle<String> key = MakeString(property.string, expected);
    if (key.is_identical_to(expected)) {
      if (descriptor < feedback_descriptors) target = feedback;
    } else {
      if (descriptor < feedback_descriptors) {
        map = ParentOfDescriptorOwner(isolate_, map, feedback, descriptor);
        feedback_descriptors = 0;
      }
      if (!TransitionsAccessor(isolate(), map)
               .FindTransitionToField(key)
               .ToHandle(&target)) {
        break;
      }
    }

    Handle<Object> value = property.value;

    PropertyDetails details =
        target->instance_descriptors().GetDetails(descriptor);
    Representation expected_representation = details.representation();

    if (!value->FitsRepresentation(expected_representation)) {
      Representation representation = value->OptimalRepresentation(isolate());
      representation = representation.generalize(expected_representation);
      if (!expected_representation.CanBeInPlaceChangedTo(representation)) {
        map = ParentOfDescriptorOwner(isolate_, map, target, descriptor);
        break;
      }
      Handle<FieldType> value_type =
          value->OptimalType(isolate(), representation);
      Map::GeneralizeField(isolate(), target, descriptor, details.constness(),
                           representation, value_type);
    } else if (expected_representation.IsHeapObject() &&
               !target->instance_descriptors()
                    .GetFieldType(descriptor)
                    .NowContains(value)) {
      Handle<FieldType> value_type =
          value->OptimalType(isolate(), expected_representation);
      Map::GeneralizeField(isolate(), target, descriptor, details.constness(),
                           expected_representation, value_type);
    } else if (!FLAG_unbox_double_fields &&
               expected_representation.IsDouble() && value->IsSmi()) {
      new_mutable_double++;
    }

    DCHECK(target->instance_descriptors()
               .GetFieldType(descriptor)
               .NowContains(value));
    map = target;
    descriptor++;
  }

  // Fast path: Write all transitioned named properties.
  if (i == length && descriptor < feedback_descriptors) {
    map = ParentOfDescriptorOwner(isolate_, map, map, descriptor);
  }

  // Preallocate all mutable heap numbers so we don't need to allocate while
  // setting up the object. Otherwise verification of that object may fail.
  Handle<ByteArray> mutable_double_buffer;
  // Allocate enough space so we can double-align the payload.
  const int kMutableDoubleSize = sizeof(double) * 2;
  STATIC_ASSERT(HeapNumber::kSize <= kMutableDoubleSize);
  if (new_mutable_double > 0) {
    mutable_double_buffer =
        factory()->NewByteArray(kMutableDoubleSize * new_mutable_double);
  }

  Handle<JSObject> object = initial_map->is_dictionary_map()
                                ? factory()->NewSlowJSObjectFromMap(map)
                                : factory()->NewJSObjectFromMap(map);
  object->set_elements(*elements);

  {
    descriptor = 0;
    DisallowHeapAllocation no_gc;
    WriteBarrierMode mode = object->GetWriteBarrierMode(no_gc);
    Address mutable_double_address =
        mutable_double_buffer.is_null()
            ? 0
            : reinterpret_cast<Address>(
                  mutable_double_buffer->GetDataStartAddress());
    Address filler_address = mutable_double_address;
    if (IsAligned(mutable_double_address, kDoubleAlignment)) {
      mutable_double_address += kTaggedSize;
    } else {
      filler_address += HeapNumber::kSize;
    }
    for (int j = 0; j < i; j++) {
      const JsonProperty& property = property_stack[start + j];
      if (property.string.is_index()) continue;
      PropertyDetails details =
          map->instance_descriptors().GetDetails(descriptor);
      Object value = *property.value;
      FieldIndex index = FieldIndex::ForDescriptor(*map, descriptor);
      descriptor++;

      if (details.representation().IsDouble()) {
        if (object->IsUnboxedDoubleField(index)) {
          uint64_t bits;
          if (value.IsSmi()) {
            bits = bit_cast<uint64_t>(static_cast<double>(Smi::ToInt(value)));
          } else {
            DCHECK(value.IsHeapNumber());
            bits = HeapNumber::cast(value).value_as_bits();
          }
          object->RawFastDoublePropertyAsBitsAtPut(index, bits);
          continue;
        }

        if (value.IsSmi()) {
          if (kTaggedSize != kDoubleSize) {
            // Write alignment filler.
            HeapObject filler = HeapObject::FromAddress(filler_address);
            filler.set_map_after_allocation(
                *factory()->one_pointer_filler_map());
            filler_address += kMutableDoubleSize;
          }

          uint64_t bits =
              bit_cast<uint64_t>(static_cast<double>(Smi::ToInt(value)));
          // Allocate simple heapnumber with immortal map, with non-pointer
          // payload, so we can skip notifying object layout change.

          HeapObject hn = HeapObject::FromAddress(mutable_double_address);
          hn.set_map_after_allocation(*factory()->heap_number_map());
          HeapNumber::cast(hn).set_value_as_bits(bits);
          value = hn;
          mutable_double_address += kMutableDoubleSize;
        } else {
          DCHECK(value.IsHeapNumber());
          HeapObject::cast(value).synchronized_set_map(
              *factory()->heap_number_map());
        }
      }
      object->RawFastInobjectPropertyAtPut(index, value, mode);
    }
    // Make all mutable HeapNumbers alive.
    if (!mutable_double_buffer.is_null()) {
#ifdef DEBUG
      Address end =
          reinterpret_cast<Address>(mutable_double_buffer->GetDataEndAddress());
      DCHECK_EQ(Min(filler_address, mutable_double_address), end);
      DCHECK_GE(filler_address, end);
      DCHECK_GE(mutable_double_address, end);
#endif
      mutable_double_buffer->set_length(0);
    }
  }

  // Slow path: define remaining named properties.
  for (; i < length; i++) {
    HandleScope scope(isolate_);
    const JsonProperty& property = property_stack[start + i];
    if (property.string.is_index()) continue;
    Handle<String> key = MakeString(property.string);
#ifdef DEBUG
    uint32_t index;
    DCHECK(!key->AsArrayIndex(&index));
#endif
    Handle<Object> value = property.value;
    LookupIterator it(isolate_, object, key, object, LookupIterator::OWN);
    JSObject::DefineOwnPropertyIgnoreAttributes(&it, value, NONE).Check();
  }

  return object;
}


MaybeHandle<Object> JsonStringify2(Isolate* isolate, Handle<Object> object,
                                  Handle<Object> replacer, Handle<Object> gap) {
  // TODO Stack limit handling as in Serialize_
  //return object;

  if (object->IsSmi()) {
    return object;
  }
  switch (HeapObject::cast(*object).map().instance_type()) {
    case HEAP_NUMBER_TYPE:
      return object;
    case BIGINT_TYPE:
      return object;
    case ODDBALL_TYPE:
      return object;
    case JS_ARRAY_TYPE: {
      // TODO Fast version
      /*JSONStringifier2 stringifier(isolate);
      stringifier.SerializeJSArray(Handle<JSArray>::cast(object), (stringifier.factory())->empty_string());
      return stringifier.builder_.Finish();*/
      Handle<JSArray> array = Handle<JSArray>::cast(object);
      //return object; WORKS

      uint32_t length = 0;
      CHECK(array->length().ToArrayLength(&length));
      //return object; WORKS

      JSONStringifier2 stringifier(isolate); // needed just for stringifier.factory
      Handle<JSArray> array2 = stringifier.factory()->NewJSArray(PACKED_SMI_ELEMENTS, length, length);


      DisallowHeapAllocation no_gc;
      FixedArray elements = FixedArray::cast(array2->elements());
      WriteBarrierMode mode = elements.GetWriteBarrierMode(no_gc);

      JSONStringifier2 tmp(isolate);
      for (uint32_t i = 0; i < length; i++) {
        Handle<Object> element;
        Handle<JSReceiver> object2 = Handle<JSReceiver>::cast(object);
        // Figure out this return on exception thing
        // ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        //   isolate, element, JSReceiver::GetElement(isolate, array, i),
        //   tmp.EXCEPTION);

        // TODO^ for above^
        
        // ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        //   isolate, element, JSReceiver::GetElement(isolate, object2, i),
        //   JSONStringifier2::EXCEPTION);
        (JSReceiver::GetElement(isolate, object2, i)).ToHandle(&element);

        Handle<Object> element2;
        JsonStringify2(isolate, element, replacer, gap).ToHandle(&element2);

        elements.set(i, *element2, mode);
        //return object; WORKS
        //JSONStringifier2::Result result = tmp.SerializeElement(isolate, element, length - 1 - i);
        // return object; WORKS NOW AFTER FIX
        //if (result == JSONStringifier2::SUCCESS) continue;
        //return object; DOESNT WORK
      }

      return array2;
    }
    case JS_PRIMITIVE_WRAPPER_TYPE:
      // TODO
    default:
      if (object->IsString() || object->IsCallable()) {
        return object;
      }
      if (object->IsJSProxy()) {
        // TODO
      }
      // TODO SerializeJSObject
      JSONStringifier2 stringifier(isolate);
      Handle<JSFunction> object_constructor_((stringifier.isolate_)->object_function());
      JsonContinuation cont = JsonContinuation(stringifier.isolate_, JsonContinuation::kObjectProperty, 0);
      std::vector<JsonProperty> property_stack;
      JsonString
      //JsonProperty(JsonString)
      property_stack.emplace_back()
      Handle<Object> obj = BuildJsonObject(cont, );

      return stringifier.factory()->NewJSObject(object_constructor_);
  }

  // Should never reach this point
  return object;
  
  /*return object;
  return Handle<String>::cast(object);
  JSONStringifier2 stringifier(isolate);
  return stringifier.Stringify(object, replacer, gap);*/
}

// Translation table to escape Latin1 characters.
// Table entries start at a multiple of 8 and are null-terminated.
const char* const JSONStringifier2::JsonEscapeTable =
    "\\u0000\0 \\u0001\0 \\u0002\0 \\u0003\0 "
    "\\u0004\0 \\u0005\0 \\u0006\0 \\u0007\0 "
    "\\b\0     \\t\0     \\n\0     \\u000b\0 "
    "\\f\0     \\r\0     \\u000e\0 \\u000f\0 "
    "\\u0010\0 \\u0011\0 \\u0012\0 \\u0013\0 "
    "\\u0014\0 \\u0015\0 \\u0016\0 \\u0017\0 "
    "\\u0018\0 \\u0019\0 \\u001a\0 \\u001b\0 "
    "\\u001c\0 \\u001d\0 \\u001e\0 \\u001f\0 "
    " \0      !\0      \\\"\0     #\0      "
    "$\0      %\0      &\0      '\0      "
    "(\0      )\0      *\0      +\0      "
    ",\0      -\0      .\0      /\0      "
    "0\0      1\0      2\0      3\0      "
    "4\0      5\0      6\0      7\0      "
    "8\0      9\0      :\0      ;\0      "
    "<\0      =\0      >\0      ?\0      "
    "@\0      A\0      B\0      C\0      "
    "D\0      E\0      F\0      G\0      "
    "H\0      I\0      J\0      K\0      "
    "L\0      M\0      N\0      O\0      "
    "P\0      Q\0      R\0      S\0      "
    "T\0      U\0      V\0      W\0      "
    "X\0      Y\0      Z\0      [\0      "
    "\\\\\0     ]\0      ^\0      _\0      "
    "`\0      a\0      b\0      c\0      "
    "d\0      e\0      f\0      g\0      "
    "h\0      i\0      j\0      k\0      "
    "l\0      m\0      n\0      o\0      "
    "p\0      q\0      r\0      s\0      "
    "t\0      u\0      v\0      w\0      "
    "x\0      y\0      z\0      {\0      "
    "|\0      }\0      ~\0      \x7F\0      "
    "\x80\0      \x81\0      \x82\0      \x83\0      "
    "\x84\0      \x85\0      \x86\0      \x87\0      "
    "\x88\0      \x89\0      \x8A\0      \x8B\0      "
    "\x8C\0      \x8D\0      \x8E\0      \x8F\0      "
    "\x90\0      \x91\0      \x92\0      \x93\0      "
    "\x94\0      \x95\0      \x96\0      \x97\0      "
    "\x98\0      \x99\0      \x9A\0      \x9B\0      "
    "\x9C\0      \x9D\0      \x9E\0      \x9F\0      "
    "\xA0\0      \xA1\0      \xA2\0      \xA3\0      "
    "\xA4\0      \xA5\0      \xA6\0      \xA7\0      "
    "\xA8\0      \xA9\0      \xAA\0      \xAB\0      "
    "\xAC\0      \xAD\0      \xAE\0      \xAF\0      "
    "\xB0\0      \xB1\0      \xB2\0      \xB3\0      "
    "\xB4\0      \xB5\0      \xB6\0      \xB7\0      "
    "\xB8\0      \xB9\0      \xBA\0      \xBB\0      "
    "\xBC\0      \xBD\0      \xBE\0      \xBF\0      "
    "\xC0\0      \xC1\0      \xC2\0      \xC3\0      "
    "\xC4\0      \xC5\0      \xC6\0      \xC7\0      "
    "\xC8\0      \xC9\0      \xCA\0      \xCB\0      "
    "\xCC\0      \xCD\0      \xCE\0      \xCF\0      "
    "\xD0\0      \xD1\0      \xD2\0      \xD3\0      "
    "\xD4\0      \xD5\0      \xD6\0      \xD7\0      "
    "\xD8\0      \xD9\0      \xDA\0      \xDB\0      "
    "\xDC\0      \xDD\0      \xDE\0      \xDF\0      "
    "\xE0\0      \xE1\0      \xE2\0      \xE3\0      "
    "\xE4\0      \xE5\0      \xE6\0      \xE7\0      "
    "\xE8\0      \xE9\0      \xEA\0      \xEB\0      "
    "\xEC\0      \xED\0      \xEE\0      \xEF\0      "
    "\xF0\0      \xF1\0      \xF2\0      \xF3\0      "
    "\xF4\0      \xF5\0      \xF6\0      \xF7\0      "
    "\xF8\0      \xF9\0      \xFA\0      \xFB\0      "
    "\xFC\0      \xFD\0      \xFE\0      \xFF\0      ";

JSONStringifier2::JSONStringifier2(Isolate* isolate)
    : isolate_(isolate),
      builder_(isolate),
      gap_(nullptr),
      indent_(0),
      stack_() {
  tojson_string_ = factory()->toJSON_string();
}

MaybeHandle<Object> JSONStringifier2::Stringify(Handle<Object> object,
                                               Handle<Object> replacer,
                                               Handle<Object> gap) {
  if (!InitializeReplacer(replacer)) return MaybeHandle<Object>();
  if (!gap->IsUndefined(isolate_) && !InitializeGap(gap)) {
    return MaybeHandle<Object>();
  }
  Result result = SerializeObject(object);
  if (result == UNCHANGED) return factory()->undefined_value();
  if (result == SUCCESS) return builder_.Finish();
  DCHECK(result == EXCEPTION);
  return MaybeHandle<Object>();
}

bool JSONStringifier2::InitializeReplacer(Handle<Object> replacer) {
  DCHECK(property_list_.is_null());
  DCHECK(replacer_function_.is_null());
  Maybe<bool> is_array = Object::IsArray(replacer);
  if (is_array.IsNothing()) return false;
  if (is_array.FromJust()) {
    HandleScope handle_scope(isolate_);
    Handle<OrderedHashSet> set = factory()->NewOrderedHashSet();
    Handle<Object> length_obj;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, length_obj,
        Object::GetLengthFromArrayLike(isolate_,
                                       Handle<JSReceiver>::cast(replacer)),
        false);
    uint32_t length;
    if (!length_obj->ToUint32(&length)) length = kMaxUInt32;
    for (uint32_t i = 0; i < length; i++) {
      Handle<Object> element;
      Handle<String> key;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, element, Object::GetElement(isolate_, replacer, i), false);
      if (element->IsNumber() || element->IsString()) {
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate_, key, Object::ToString(isolate_, element), false);
      } else if (element->IsJSPrimitiveWrapper()) {
        Handle<Object> value(Handle<JSPrimitiveWrapper>::cast(element)->value(),
                             isolate_);
        if (value->IsNumber() || value->IsString()) {
          ASSIGN_RETURN_ON_EXCEPTION_VALUE(
              isolate_, key, Object::ToString(isolate_, element), false);
        }
      }
      if (key.is_null()) continue;
      // Object keys are internalized, so do it here.
      key = factory()->InternalizeString(key);
      MaybeHandle<OrderedHashSet> set_candidate =
          OrderedHashSet::Add(isolate_, set, key);
      if (!set_candidate.ToHandle(&set)) {
        return false;
      }
    }
    property_list_ = OrderedHashSet::ConvertToKeysArray(
        isolate_, set, GetKeysConversion::kKeepNumbers);
    property_list_ = handle_scope.CloseAndEscape(property_list_);
  } else if (replacer->IsCallable()) {
    replacer_function_ = Handle<JSReceiver>::cast(replacer);
  }
  return true;
}

bool JSONStringifier2::InitializeGap(Handle<Object> gap) {
  DCHECK_NULL(gap_);
  HandleScope scope(isolate_);
  if (gap->IsJSPrimitiveWrapper()) {
    Handle<Object> value(Handle<JSPrimitiveWrapper>::cast(gap)->value(),
                         isolate_);
    if (value->IsString()) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, gap,
                                       Object::ToString(isolate_, gap), false);
    } else if (value->IsNumber()) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, gap,
                                       Object::ToNumber(isolate_, gap), false);
    }
  }

  if (gap->IsString()) {
    Handle<String> gap_string = Handle<String>::cast(gap);
    if (gap_string->length() > 0) {
      int gap_length = std::min(gap_string->length(), 10);
      gap_ = NewArray<uc16>(gap_length + 1);
      String::WriteToFlat(*gap_string, gap_, 0, gap_length);
      for (int i = 0; i < gap_length; i++) {
        if (gap_[i] > String::kMaxOneByteCharCode) {
          builder_.ChangeEncoding();
          break;
        }
      }
      gap_[gap_length] = '\0';
    }
  } else if (gap->IsNumber()) {
    int num_value = DoubleToInt32(gap->Number());
    if (num_value > 0) {
      int gap_length = std::min(num_value, 10);
      gap_ = NewArray<uc16>(gap_length + 1);
      for (int i = 0; i < gap_length; i++) gap_[i] = ' ';
      gap_[gap_length] = '\0';
    }
  }
  return true;
}

MaybeHandle<Object> JSONStringifier2::ApplyToJsonFunction(Handle<Object> object,
                                                         Handle<Object> key) {
  HandleScope scope(isolate_);

  // Retrieve toJSON function. The LookupIterator automatically handles
  // the ToObject() equivalent ("GetRoot") if {object} is a BigInt.
  Handle<Object> fun;
  LookupIterator it(isolate_, object, tojson_string_,
                    LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  ASSIGN_RETURN_ON_EXCEPTION(isolate_, fun, Object::GetProperty(&it), Object);
  if (!fun->IsCallable()) return object;

  // Call toJSON function.
  if (key->IsSmi()) key = factory()->NumberToString(key);
  Handle<Object> argv[] = {key};
  ASSIGN_RETURN_ON_EXCEPTION(isolate_, object,
                             Execution::Call(isolate_, fun, object, 1, argv),
                             Object);
  return scope.CloseAndEscape(object);
}

MaybeHandle<Object> JSONStringifier2::ApplyReplacerFunction(
    Handle<Object> value, Handle<Object> key, Handle<Object> initial_holder) {
  HandleScope scope(isolate_);
  if (key->IsSmi()) key = factory()->NumberToString(key);
  Handle<Object> argv[] = {key, value};
  Handle<JSReceiver> holder = CurrentHolder(value, initial_holder);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate_, value,
      Execution::Call(isolate_, replacer_function_, holder, 2, argv), Object);
  return scope.CloseAndEscape(value);
}

Handle<JSReceiver> JSONStringifier2::CurrentHolder(
    Handle<Object> value, Handle<Object> initial_holder) {
  if (stack_.empty()) {
    Handle<JSObject> holder =
        factory()->NewJSObject(isolate_->object_function());
    JSObject::AddProperty(isolate_, holder, factory()->empty_string(),
                          initial_holder, NONE);
    return holder;
  } else {
    return Handle<JSReceiver>(JSReceiver::cast(*stack_.back().second),
                              isolate_);
  }
}

JSONStringifier2::Result JSONStringifier2::StackPush(Handle<Object> object,
                                                   Handle<Object> key) {
  StackLimitCheck check(isolate_);
  if (check.HasOverflowed()) {
    isolate_->StackOverflow();
    return EXCEPTION;
  }

  {
    DisallowHeapAllocation no_allocation;
    for (size_t i = 0; i < stack_.size(); ++i) {
      if (*stack_[i].second == *object) {
        AllowHeapAllocation allow_to_return_error;
        Handle<String> circle_description =
            ConstructCircularStructureErrorMessage(key, i);
        Handle<Object> error = factory()->NewTypeError(
            MessageTemplate::kCircularStructure, circle_description);
        isolate_->Throw(*error);
        return EXCEPTION;
      }
    }
  }
  stack_.emplace_back(key, object);
  return SUCCESS;
}

void JSONStringifier2::StackPop() { stack_.pop_back(); }

class CircularStructureMessageBuilder {
 public:
  explicit CircularStructureMessageBuilder(Isolate* isolate)
      : builder_(isolate) {}

  void AppendStartLine(Handle<Object> start_object) {
    builder_.AppendCString(kStartPrefix);
    builder_.AppendCString("starting at object with constructor ");
    AppendConstructorName(start_object);
  }

  void AppendNormalLine(Handle<Object> key, Handle<Object> object) {
    builder_.AppendCString(kLinePrefix);
    AppendKey(key);
    builder_.AppendCString(" -> object with constructor ");
    AppendConstructorName(object);
  }

  void AppendClosingLine(Handle<Object> closing_key) {
    builder_.AppendCString(kEndPrefix);
    AppendKey(closing_key);
    builder_.AppendCString(" closes the circle");
  }

  void AppendEllipsis() {
    builder_.AppendCString(kLinePrefix);
    builder_.AppendCString("...");
  }

  MaybeHandle<String> Finish() { return builder_.Finish(); }

 private:
  void AppendConstructorName(Handle<Object> object) {
    builder_.AppendCharacter('\'');
    Handle<String> constructor_name =
        JSReceiver::GetConstructorName(Handle<JSReceiver>::cast(object));
    builder_.AppendString(constructor_name);
    builder_.AppendCharacter('\'');
  }

  // A key can either be a string, the empty string or a Smi.
  void AppendKey(Handle<Object> key) {
    if (key->IsSmi()) {
      builder_.AppendCString("index ");
      AppendSmi(Smi::cast(*key));
      return;
    }

    CHECK(key->IsString());
    Handle<String> key_as_string = Handle<String>::cast(key);
    if (key_as_string->length() == 0) {
      builder_.AppendCString("<anonymous>");
    } else {
      builder_.AppendCString("property '");
      builder_.AppendString(key_as_string);
      builder_.AppendCharacter('\'');
    }
  }

  void AppendSmi(Smi smi) {
    static const int kBufferSize = 100;
    char chars[kBufferSize];
    Vector<char> buffer(chars, kBufferSize);
    builder_.AppendCString(IntToCString(smi.value(), buffer));
  }

  IncrementalStringBuilder builder_;
  static constexpr const char* kStartPrefix = "\n    --> ";
  static constexpr const char* kEndPrefix = "\n    --- ";
  static constexpr const char* kLinePrefix = "\n    |     ";
};

Handle<String> JSONStringifier2::ConstructCircularStructureErrorMessage(
    Handle<Object> last_key, size_t start_index) {
  DCHECK(start_index < stack_.size());
  CircularStructureMessageBuilder builder(isolate_);

  // We track the index to be printed next for better readability.
  size_t index = start_index;
  const size_t stack_size = stack_.size();

  builder.AppendStartLine(stack_[index++].second);

  // Append a maximum of kCircularErrorMessagePrefixCount normal lines.
  const size_t prefix_end =
      std::min(stack_size, index + kCircularErrorMessagePrefixCount);
  for (; index < prefix_end; ++index) {
    builder.AppendNormalLine(stack_[index].first, stack_[index].second);
  }

  // If the circle consists of too many objects, we skip them and just
  // print an ellipsis.
  if (stack_size > index + kCircularErrorMessagePostfixCount) {
    builder.AppendEllipsis();
  }

  // Since we calculate the postfix lines from the back of the stack,
  // we have to ensure that lines are not printed twice.
  index = std::max(index, stack_size - kCircularErrorMessagePostfixCount);
  for (; index < stack_size; ++index) {
    builder.AppendNormalLine(stack_[index].first, stack_[index].second);
  }

  builder.AppendClosingLine(last_key);

  Handle<String> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, result, builder.Finish(),
                                   factory()->empty_string());
  return result;
}

// TODO: Handle deferred_string_key. What does it do?
template <bool deferred_string_key>
JSONStringifier2::Result JSONStringifier2::Serialize_(Handle<Object> object,
                                                    bool comma,
                                                    Handle<Object> key) {
  StackLimitCheck interrupt_check(isolate_);
  Handle<Object> initial_value = object;
  if (interrupt_check.InterruptRequested() &&
      isolate_->stack_guard()->HandleInterrupts().IsException(isolate_)) {
    return EXCEPTION;
  }
  if (object->IsJSReceiver() || object->IsBigInt()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, object, ApplyToJsonFunction(object, key), EXCEPTION);
  }
  if (!replacer_function_.is_null()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, object, ApplyReplacerFunction(object, key, initial_value),
        EXCEPTION);
  }

  if (object->IsSmi()) {
    if (deferred_string_key) SerializeDeferredKey(comma, key);
    return SerializeSmi(Smi::cast(*object));
  }

  switch (HeapObject::cast(*object).map().instance_type()) {
    case HEAP_NUMBER_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeHeapNumber(Handle<HeapNumber>::cast(object));
    case BIGINT_TYPE:
      isolate_->Throw(
          *factory()->NewTypeError(MessageTemplate::kBigIntSerializeJSON));
      return EXCEPTION;
    case ODDBALL_TYPE:
      switch (Oddball::cast(*object).kind()) {
        case Oddball::kFalse:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          builder_.AppendCString("false");
          return SUCCESS;
        case Oddball::kTrue:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          builder_.AppendCString("true");
          return SUCCESS;
        case Oddball::kNull:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          builder_.AppendCString("null");
          return SUCCESS;
        default:
          return UNCHANGED;
      }
    case JS_ARRAY_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeJSArray(Handle<JSArray>::cast(object), key);
    case JS_PRIMITIVE_WRAPPER_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeJSPrimitiveWrapper(
          Handle<JSPrimitiveWrapper>::cast(object), key);
    case SYMBOL_TYPE:
      return UNCHANGED;
    default:
      if (object->IsString()) {
        if (deferred_string_key) SerializeDeferredKey(comma, key);
        SerializeString(Handle<String>::cast(object));
        return SUCCESS;
      } else {
        DCHECK(object->IsJSReceiver());
        if (object->IsCallable()) return UNCHANGED;
        // Go to slow path for global proxy and objects requiring access checks.
        if (deferred_string_key) SerializeDeferredKey(comma, key);
        if (object->IsJSProxy()) {
          return SerializeJSProxy(Handle<JSProxy>::cast(object), key);
        }
        return SerializeJSObject(Handle<JSObject>::cast(object), key);
      }
  }

  UNREACHABLE();
}

JSONStringifier2::Result JSONStringifier2::SerializeJSPrimitiveWrapper(
    Handle<JSPrimitiveWrapper> object, Handle<Object> key) {
  Object raw = object->value();
  if (raw.IsString()) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, value, Object::ToString(isolate_, object), EXCEPTION);
    SerializeString(Handle<String>::cast(value));
  } else if (raw.IsNumber()) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, value, Object::ToNumber(isolate_, object), EXCEPTION);
    if (value->IsSmi()) return SerializeSmi(Smi::cast(*value));
    SerializeHeapNumber(Handle<HeapNumber>::cast(value));
  } else if (raw.IsBigInt()) {
    isolate_->Throw(
        *factory()->NewTypeError(MessageTemplate::kBigIntSerializeJSON));
    return EXCEPTION;
  } else if (raw.IsBoolean()) {
    builder_.AppendCString(raw.IsTrue(isolate_) ? "true" : "false");
  } else {
    // ES6 24.3.2.1 step 10.c, serialize as an ordinary JSObject.
    return SerializeJSObject(object, key);
  }
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeSmi(Smi object) {
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  Vector<char> buffer(chars, kBufferSize);
  builder_.AppendCString(IntToCString(object.value(), buffer));
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeDouble(double number) {
  if (std::isinf(number) || std::isnan(number)) {
    builder_.AppendCString("null");
    return SUCCESS;
  }
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  Vector<char> buffer(chars, kBufferSize);
  builder_.AppendCString(DoubleToCString(number, buffer));
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeJSArray(
    Handle<JSArray> object, Handle<Object> key) {
  HandleScope handle_scope(isolate_);
  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;
  uint32_t length = 0;
  CHECK(object->length().ToArrayLength(&length));
  DCHECK(!object->IsAccessCheckNeeded());
  builder_.AppendCharacter('[');
  Indent();
  uint32_t i = 0;
  if (replacer_function_.is_null()) {
    switch (object->GetElementsKind()) {
      case PACKED_SMI_ELEMENTS: {
        Handle<FixedArray> elements(FixedArray::cast(object->elements()),
                                    isolate_);
        StackLimitCheck interrupt_check(isolate_);
        while (i < length) {
          if (interrupt_check.InterruptRequested() &&
              isolate_->stack_guard()->HandleInterrupts().IsException(
                  isolate_)) {
            return EXCEPTION;
          }
          Separator(i == 0);
          SerializeSmi(Smi::cast(elements->get(i)));
          i++;
        }
        break;
      }
      case PACKED_DOUBLE_ELEMENTS: {
        // Empty array is FixedArray but not FixedDoubleArray.
        if (length == 0) break;
        Handle<FixedDoubleArray> elements(
            FixedDoubleArray::cast(object->elements()), isolate_);
        StackLimitCheck interrupt_check(isolate_);
        while (i < length) {
          if (interrupt_check.InterruptRequested() &&
              isolate_->stack_guard()->HandleInterrupts().IsException(
                  isolate_)) {
            return EXCEPTION;
          }
          Separator(i == 0);
          SerializeDouble(elements->get_scalar(i));
          i++;
        }
        break;
      }
      case PACKED_ELEMENTS: {
        Handle<Object> old_length(object->length(), isolate_);
        while (i < length) {
          if (object->length() != *old_length ||
              object->GetElementsKind() != PACKED_ELEMENTS) {
            // Fall back to slow path.
            break;
          }
          Separator(i == 0);
          Result result = SerializeElement(
              isolate_,
              Handle<Object>(FixedArray::cast(object->elements()).get(i),
                             isolate_),
              i);
          if (result == UNCHANGED) {
            builder_.AppendCString("null");
          } else if (result != SUCCESS) {
            return result;
          }
          i++;
        }
        break;
      }
      // The FAST_HOLEY_* cases could be handled in a faster way. They resemble
      // the non-holey cases except that a lookup is necessary for holes.
      default:
        break;
    }
  }
  if (i < length) {
    // Slow path for non-fast elements and fall-back in edge case.
    Result result = SerializeArrayLikeSlow(object, i, length);
    if (result != SUCCESS) return result;
  }
  Unindent();
  if (length > 0) NewLine();
  builder_.AppendCharacter(']');
  StackPop();
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeArrayLikeSlow(
    Handle<JSReceiver> object, uint32_t start, uint32_t length) {
  // We need to write out at least two characters per array element.
  static const int kMaxSerializableArrayLength = String::kMaxLength / 2;
  if (length > kMaxSerializableArrayLength) {
    isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
    return EXCEPTION;
  }
  for (uint32_t i = start; i < length; i++) {
    Separator(i == 0);
    Handle<Object> element;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, element, JSReceiver::GetElement(isolate_, object, i),
        EXCEPTION);
    Result result = SerializeElement(isolate_, element, i);
    if (result == SUCCESS) continue;
    if (result == UNCHANGED) {
      // Detect overflow sooner for large sparse arrays.
      if (builder_.HasOverflowed()) return EXCEPTION;
      builder_.AppendCString("null");
    } else {
      return result;
    }
  }
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeJSObject(
    Handle<JSObject> object, Handle<Object> key) {
  HandleScope handle_scope(isolate_);
  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;

  if (property_list_.is_null() &&
      !object->map().IsCustomElementsReceiverMap() &&
      object->HasFastProperties() &&
      (object->elements() == ReadOnlyRoots(isolate_).empty_fixed_array() ||
       object->elements() ==
           ReadOnlyRoots(isolate_).empty_slow_element_dictionary())) {
    DCHECK(!object->IsJSGlobalProxy());
    DCHECK(!object->HasIndexedInterceptor());
    DCHECK(!object->HasNamedInterceptor());
    Handle<Map> map(object->map(), isolate_);
    builder_.AppendCharacter('{');
    Indent();
    bool comma = false;
    for (int i = 0; i < map->NumberOfOwnDescriptors(); i++) {
      Handle<Name> name(map->instance_descriptors().GetKey(i), isolate_);
      // TODO(rossberg): Should this throw?
      if (!name->IsString()) continue;
      Handle<String> key = Handle<String>::cast(name);
      PropertyDetails details = map->instance_descriptors().GetDetails(i);
      if (details.IsDontEnum()) continue;
      Handle<Object> property;
      if (details.location() == kField && *map == object->map()) {
        DCHECK_EQ(kData, details.kind());
        FieldIndex field_index = FieldIndex::ForDescriptor(*map, i);
        property = JSObject::FastPropertyAt(object, details.representation(),
                                            field_index);
      } else {
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate_, property,
            Object::GetPropertyOrElement(isolate_, object, key), EXCEPTION);
      }
      Result result = SerializeProperty(property, comma, key);
      if (!comma && result == SUCCESS) comma = true;
      if (result == EXCEPTION) return result;
    }
    Unindent();
    if (comma) NewLine();
    builder_.AppendCharacter('}');
  } else {
    Result result = SerializeJSReceiverSlow(object);
    if (result != SUCCESS) return result;
  }
  StackPop();
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeJSReceiverSlow(
    Handle<JSReceiver> object) {
  Handle<FixedArray> contents = property_list_;
  if (contents.is_null()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, contents,
        KeyAccumulator::GetKeys(object, KeyCollectionMode::kOwnOnly,
                                ENUMERABLE_STRINGS,
                                GetKeysConversion::kConvertToString),
        EXCEPTION);
  }
  builder_.AppendCharacter('{');
  Indent();
  bool comma = false;
  for (int i = 0; i < contents->length(); i++) {
    Handle<String> key(String::cast(contents->get(i)), isolate_);
    Handle<Object> property;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, property, Object::GetPropertyOrElement(isolate_, object, key),
        EXCEPTION);
    Result result = SerializeProperty(property, comma, key);
    if (!comma && result == SUCCESS) comma = true;
    if (result == EXCEPTION) return result;
  }
  Unindent();
  if (comma) NewLine();
  builder_.AppendCharacter('}');
  return SUCCESS;
}

JSONStringifier2::Result JSONStringifier2::SerializeJSProxy(
    Handle<JSProxy> object, Handle<Object> key) {
  HandleScope scope(isolate_);
  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;
  Maybe<bool> is_array = Object::IsArray(object);
  if (is_array.IsNothing()) return EXCEPTION;
  if (is_array.FromJust()) {
    Handle<Object> length_object;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, length_object,
        Object::GetLengthFromArrayLike(isolate_,
                                       Handle<JSReceiver>::cast(object)),
        EXCEPTION);
    uint32_t length;
    if (!length_object->ToUint32(&length)) {
      // Technically, we need to be able to handle lengths outside the
      // uint32_t range. However, we would run into string size overflow
      // if we tried to stringify such an array.
      isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
      return EXCEPTION;
    }
    builder_.AppendCharacter('[');
    Indent();
    Result result = SerializeArrayLikeSlow(object, 0, length);
    if (result != SUCCESS) return result;
    Unindent();
    if (length > 0) NewLine();
    builder_.AppendCharacter(']');
  } else {
    Result result = SerializeJSReceiverSlow(object);
    if (result != SUCCESS) return result;
  }
  StackPop();
  return SUCCESS;
}

template <typename SrcChar, typename DestChar>
void JSONStringifier2::SerializeStringUnchecked_(
    Vector<const SrcChar> src,
    IncrementalStringBuilder::NoExtend<DestChar>* dest) {
  // Assert that uc16 character is not truncated down to 8 bit.
  // The <uc16, char> version of this method must not be called.
  DCHECK(sizeof(DestChar) >= sizeof(SrcChar));
  for (int i = 0; i < src.length(); i++) {
    SrcChar c = src[i];
    if (DoNotEscape(c)) {
      dest->Append(c);
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      // The current character is a surrogate.
      if (c <= 0xDBFF) {
        // The current character is a leading surrogate.
        if (i + 1 < src.length()) {
          // There is a next character.
          SrcChar next = src[i + 1];
          if (next >= 0xDC00 && next <= 0xDFFF) {
            // The next character is a trailing surrogate, meaning this is a
            // surrogate pair.
            dest->Append(c);
            dest->Append(next);
            i++;
          } else {
            // The next character is not a trailing surrogate. Thus, the
            // current character is a lone leading surrogate.
            dest->AppendCString("\\u");
            char* const hex = DoubleToRadixCString(c, 16);
            dest->AppendCString(hex);
            DeleteArray(hex);
          }
        } else {
          // There is no next character. Thus, the current character is a lone
          // leading surrogate.
          dest->AppendCString("\\u");
          char* const hex = DoubleToRadixCString(c, 16);
          dest->AppendCString(hex);
          DeleteArray(hex);
        }
      } else {
        // The current character is a lone trailing surrogate. (If it had been
        // preceded by a leading surrogate, we would've ended up in the other
        // branch earlier on, and the current character would've been handled
        // as part of the surrogate pair already.)
        dest->AppendCString("\\u");
        char* const hex = DoubleToRadixCString(c, 16);
        dest->AppendCString(hex);
        DeleteArray(hex);
      }
    } else {
      dest->AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
    }
  }
}

template <typename SrcChar, typename DestChar>
void JSONStringifier2::SerializeString_(Handle<String> string) {
  int length = string->length();
  builder_.Append<uint8_t, DestChar>('"');
  // We might be able to fit the whole escaped string in the current string
  // part, or we might need to allocate.
  if (int worst_case_length = builder_.EscapedLengthIfCurrentPartFits(length)) {
    DisallowHeapAllocation no_gc;
    Vector<const SrcChar> vector = string->GetCharVector<SrcChar>(no_gc);
    IncrementalStringBuilder::NoExtendBuilder<DestChar> no_extend(
        &builder_, worst_case_length, no_gc);
    SerializeStringUnchecked_(vector, &no_extend);
  } else {
    FlatStringReader reader(isolate_, string);
    for (int i = 0; i < reader.length(); i++) {
      SrcChar c = reader.Get<SrcChar>(i);
      if (DoNotEscape(c)) {
        builder_.Append<SrcChar, DestChar>(c);
      } else if (c >= 0xD800 && c <= 0xDFFF) {
        // The current character is a surrogate.
        if (c <= 0xDBFF) {
          // The current character is a leading surrogate.
          if (i + 1 < reader.length()) {
            // There is a next character.
            SrcChar next = reader.Get<SrcChar>(i + 1);
            if (next >= 0xDC00 && next <= 0xDFFF) {
              // The next character is a trailing surrogate, meaning this is a
              // surrogate pair.
              builder_.Append<SrcChar, DestChar>(c);
              builder_.Append<SrcChar, DestChar>(next);
              i++;
            } else {
              // The next character is not a trailing surrogate. Thus, the
              // current character is a lone leading surrogate.
              builder_.AppendCString("\\u");
              char* const hex = DoubleToRadixCString(c, 16);
              builder_.AppendCString(hex);
              DeleteArray(hex);
            }
          } else {
            // There is no next character. Thus, the current character is a
            // lone leading surrogate.
            builder_.AppendCString("\\u");
            char* const hex = DoubleToRadixCString(c, 16);
            builder_.AppendCString(hex);
            DeleteArray(hex);
          }
        } else {
          // The current character is a lone trailing surrogate. (If it had
          // been preceded by a leading surrogate, we would've ended up in the
          // other branch earlier on, and the current character would've been
          // handled as part of the surrogate pair already.)
          builder_.AppendCString("\\u");
          char* const hex = DoubleToRadixCString(c, 16);
          builder_.AppendCString(hex);
          DeleteArray(hex);
        }
      } else {
        builder_.AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
      }
    }
  }
  builder_.Append<uint8_t, DestChar>('"');
}

template <>
bool JSONStringifier2::DoNotEscape(uint8_t c) {
  // https://tc39.github.io/ecma262/#table-json-single-character-escapes
  return c >= 0x23 && c <= 0x7E && c != 0x5C;
}

template <>
bool JSONStringifier2::DoNotEscape(uint16_t c) {
  // https://tc39.github.io/ecma262/#table-json-single-character-escapes
  return c >= 0x23 && c != 0x5C && c != 0x7F && (c < 0xD800 || c > 0xDFFF);
}

void JSONStringifier2::NewLine() {
  if (gap_ == nullptr) return;
  builder_.AppendCharacter('\n');
  for (int i = 0; i < indent_; i++) builder_.AppendCString(gap_);
}

void JSONStringifier2::Separator(bool first) {
  if (!first) builder_.AppendCharacter(',');
  NewLine();
}

void JSONStringifier2::SerializeDeferredKey(bool deferred_comma,
                                           Handle<Object> deferred_key) {
  Separator(!deferred_comma);
  SerializeString(Handle<String>::cast(deferred_key));
  builder_.AppendCharacter(':');
  if (gap_ != nullptr) builder_.AppendCharacter(' ');
}

void JSONStringifier2::SerializeString(Handle<String> object) {
  object = String::Flatten(isolate_, object);
  if (builder_.CurrentEncoding() == String::ONE_BYTE_ENCODING) {
    if (String::IsOneByteRepresentationUnderneath(*object)) {
      SerializeString_<uint8_t, uint8_t>(object);
    } else {
      builder_.ChangeEncoding();
      SerializeString(object);
    }
  } else {
    if (String::IsOneByteRepresentationUnderneath(*object)) {
      SerializeString_<uint8_t, uc16>(object);
    } else {
      SerializeString_<uc16, uc16>(object);
    }
  }
}

}  // namespace internal
}  // namespace v8
