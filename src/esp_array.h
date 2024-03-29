#ifndef _ESP_ARRAY_H_
#define _ESP_ARRAY_H_

#include <Arduino.h>
#include <type_traits>
#include <esp_err.h>

#include "dsps_conv.h"
#include "dsps_corr.h"
#include "dsps_dotprod.h"

#include "esp_opt.h"
#include "esp_dsp.h"
#include "esp_ansi.h"

#include "esp_fixed_point.h"

/**
 * @brief Namespace for custom ESP32 MATH libraries
 * 
 */
namespace espmath{
  
  typedef struct FixedPointVector
  {
    int16_t* data;
    uint8_t frac;

    FixedPointVector():data(NULL),frac(0){}
    FixedPointVector(int16_t* d, uint8_t f):data(d),frac(f){}
  }fixedV;

  struct shape2D{
  private:
    size_t _rows = 1;
    size_t _columns = 0;
    size_t _size = 0;
  public:
    const size_t& rows = _rows;
    const size_t& columns = _columns;
    const size_t& size = _size;

    shape2D():_rows(1),_columns(0),_size(0){}
    shape2D(size_t r, size_t c):_rows(r),_columns(c),_size(r*c){}
    shape2D(const shape2D& another):_rows(another.rows),_columns(another.columns),_size(another.size){}
    shape2D(const shape2D&& another):shape2D(another){}
    void operator=(const shape2D& another){_rows = another.rows,_columns = another.columns,_size = another.size;}
    void operator=(const shape2D&& another){_rows = another.rows,_columns = another.columns,_size = another.size;}
    bool operator==(const shape2D& another)const{return another.rows == rows && another.columns == columns;}
    bool operator!=(const shape2D& another)const{return another.rows != rows || another.columns != columns;}
    
    /**
     * @brief Verify if shape satisfies the rules to perform matrix multiplication
     * 
     * @param another 
     * @return true 
     * @return false 
     */
    bool canX(const shape2D& another)const{return this->columns == another.rows;}

    /**
     * @brief Get the resultant shape of the matrix multiplication
     * 
     * @param another 
     * @return shape2D 
     */
    shape2D operator*(const shape2D& another)const{return shape2D(this->rows, another.columns);}
  };
  
  /**
   * @brief Custom Array implementation suitable for ESP32 devices.
   * 
   * This implements an array suitable for ESP32 boards.
   * Using this class, you can create an array of any arithmetic type.
   * 
   * @note Most of float and int16_t arithmetic operations make use of DSP instructions.
   * 
   * @tparam T Array type
   * @tparam FRAC Fractional bits of a fixes point array
   */
  template <typename T = int16_t> class Array
  {
  public:
    typedef T* const arrayPntr;

    static bool isDSPSupported(){return false;}

    /**
     * @brief Destroy the Array object
     * 
     */
    ~Array()
    {
      if (canBeDestroyed && _array)
        heap_caps_free(_array);
    }

    /**
     * @brief Construct a new Array object
     * 
     * @param initialMem The initial size of the array.
     * @param capabilities Memory capabilities.
     */
    Array(shape2D initialShape = shape2D(1,0), uint32_t capabilities = UINT32_MAX)
    {
      ESP_ERROR_CHECK(initialShape.rows == 0); //"rows must be greater than 0!"
      if (capabilities != UINT32_MAX)
        _caps = capabilities;
      _shape = initialShape;
      _size = _mem2alloc(_shape.columns);
      _array = _size > 0 ? (T*)heap_caps_aligned_alloc(ALIGNMENT, _size, _caps) : NULL;
      if(!_array)
        _size = 0;
    }

    /**
     * @brief Construct a new Array object
     * 
     * @param initFrac Fractional Bits
     * @param initialMem The initial size of the array.
     * @param capabilities Memory capabilities.
     */
    Array(const uint8_t initFrac,\
          const shape2D initialShape = shape2D(1,0),\
          const uint32_t capabilities = UINT32_MAX):Array(initialShape, capabilities)
    {
      fracBits = initFrac;
    }
    
    /**
     * @brief Construct a new Array object with initial values
     * 
     * @param initialValues Initial values of the array.
     * @param initialMem The initial size of the array.
     * @param capabilities Memory capabilities
     */
    Array(const T* initialValues,\
          const shape2D initialShape = shape2D(1,0),\
          const uint32_t capabilities = UINT32_MAX):Array(initialShape, capabilities)
    {
      if (_array && initialValues)
        cpyArray(initialValues, _array, _shape.columns);
    }

    /**
     * @brief Construct a new Array object with initial values for fixed point type
     * 
     * @param initialValues Initial values of the array.
     * @param initialMem The initial size of the array.
     * @param capabilities Memory capabilities
     */
    Array(const FixedPointVector initialValues,\
          const shape2D initialShape = shape2D(1,0),\
          const uint32_t capabilities = UINT32_MAX):Array(initialShape, capabilities)
    {
      fracBits = initialValues.frac;
      if (_array && initialValues)
        cpyArray(initialValues.data, _array, _shape.columns);
    }

    /**
     * @brief Construct a new Array object with initial values for fixed point type
     * 
     * @param initialValues Initial values of the array.
     * @param initialMem The initial size of the array.
     * @param capabilities Memory capabilities
     */
    Array(const fixed* initialValues,\
          const shape2D initialShape = shape2D(1,0),\
          const uint32_t capabilities = UINT32_MAX):Array(initialShape, capabilities)
    {
      fracBits = initialValues[0].frac;
      if (_array && initialValues)
      {
        for (int i = 0; i < _shape.columns; i++)
          _array[i] = initialValues[i].data;
      }
    }

    /**
     * @brief Constructor
     * 
     * @param another 
     */
    Array(const Array& another){copy(another);}
    Array(const Array&& another){copy(another);}

    /**
     * @brief Assign operation
     * 
     * @param another 
     */
    void operator=(const Array& another){copy(another);}
    void operator=(Array&& another){copyRef(another);}

    /**
     * @brief Get array element
     * 
     * @param index 
     * @return T 
     */
    T* operator[](const size_t index)
    {
      return &_array[_shape.columns*index];
    }

    T* operator()(const size_t i = 0)
    {
      return &_array[i];
    }

    /**
     * @brief Filter the array removing the elements where filter is false.
     * 
     * Example:
     * filter = {0, 1, 0, 1, 1};
     * array  = {1, 2, 3, 4, 5};
     * 
     * array[filter] -> {2,4,5}.
     * 
     * @param filter The array filter
     * @return Array
     */
    Array operator[](const Array filter)
    {
      Array<T> newArray();
      for (size_t i = 0; i < _shape.columns; i++)
        if(filter.flatten[i]) newArray << _array[i];
      return newArray;
    }

    /**
     * @brief Get the boolean array indicating where the value is the same as the element.
     * 
     * @param value The value to be verified.
     * @return Array.
     */
    Array operator==(const T value)
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] == value;
      
      return newArray;
    }

    Array operator==(const fixed value)
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] == value.data;
      return newArray;
    }

    /**
     * @brief Get the boolean array indicating where the value is different than the element.
     * 
     * @param value The value to be verified.
     * @return Array.
     */
    Array operator!=(const T value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] != value;
      
      return newArray;
    }

    Array operator!=(const fixed value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] != value.data;
      
      return newArray;
    }

    /**
     * @brief Get the boolean array indicating where the value is greater than the element.
     * 
     * @param value The value to be verified.
     * @return Array.
     */
    Array operator>(const T value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] > value;
      
      return newArray;
    }

    Array operator>(const fixed value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] > value.data;
      
      return newArray;
    }

    /**
     * @brief Get the boolean array indicating where the value is less than the element.
     * 
     * @param value The value to be verified.
     * @return Array.
     */
    Array operator<(const T value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] < value;
      
      return newArray;
    }

    Array operator<(const fixed value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] < value.data;
      
      return newArray;
    }

    /**
     * @brief Get the boolean array indicating where the value is greater or equal to the element.
     * 
     * @param value The value to be verified.
     * @return Array.
     */
    Array operator>=(const T value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] >= value;
      
      return newArray;
    }

    Array operator>=(const fixed value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] >= value.data;
      
      return newArray;
    }

    /**
     * @brief Get the boolean array indicating where the value is less or equal to the element.
     * 
     * @param value The value to be verified.
     * @return Array.
     */
    Array operator<=(const T value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] <= value;
      
      return newArray;
    }

    Array operator<=(const fixed value) const
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = _array[i] <= value.data;
      
      return newArray;
    }

    /**
     * @brief copy.flatten[i] = !array.flatten[i], i = 0,1,2,3...
     * 
     * @return Array 
     */
    Array operator!()
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = !_array[i];
      
      return newArray;
    }

    /**
     * @brief Perform a binary not on every element and return the result
     * 
     * @return Array 
     */
    Array operator~()
    {
      Array<T> newArray(_shape);
      for (size_t i = 0; i < _shape.size; i++)
        newArray.flatten[i] = ~_array[i];
      
      return newArray;
    }

    /**
     * @brief Verify if two arrays are identical.
     * 
     * @param input The input _array
     * @return true Every value of the _array is contained in input.
     * @return false Not all values of the _array are contained in input.
     */
    bool operator==(const T* input)
    {
      size_t i = 0;
      while( i < _shape.size)
      {
        if (_array[i] != input[i])
          return false;
        i++;
      }
      return true;
    }

    bool operator==(const fixed* input)
    {
      size_t i = 0;
      while( i < _shape.size)
      {
        if (_array[i] != input[i].data)
          return false;
        i++;
      }
      return true;
    }

    /**
     * @brief Verify if two arrays are identical.
     * 
     * @param input The input array
     * @return true Every value of the _array is contained in input.
     * @return false Not all values of the _array are contained in input.
     */
    bool operator==(const Array& another)
    {
      bool result = *this == (T*)another;
      return result;
    }
    bool operator==(const Array&& another)
    {
      bool result = *this == (T*)another;
      return result;
    }

    /**
     * @brief Verify if two arrays are not identical.
     * 
     * @param input The input _array
     * @return true Not all values of the _array are contained in input.
     * @return false Every value of the _array is contained in input.
     */
    bool operator!=(const T* input)
    {
      return !(*this == input);
    }

    /**
     * @brief Add a constant
     * 
     * @param value 
     * @note Float arrays make use of DSP instructions.
     */
    void operator+=(const T value)
    {
      addConstToArray(_array, _array, _shape.columns, value);
    }

    /**
     * @brief Subtract a constant
     * 
     * @param value 
     * @note Float arrays make use of DSP instructions.
     */
    void operator-=(const T value)
    {
      *this += value*(-1);
    }

    /**
     * @brief Multiply by a constant
     * 
     * @param value 
     * @note Float arrays make use of DSP instructions.
     */
    void operator*=(const T value)
    { 
      mulConstByArray(_array, _array, _shape.columns, value);
    }

    /**
     * @brief Divide by a constant
     * 
     * @param value 
     * @note Float arrays make use of DSP instructions.
     */
    void operator/=(const T value)
    {
      divArrayByConst(_array, _array, _shape.columns, value);
    }

    /**
     * @brief Add an array
     * 
     * @param another
     * @note Float and int16_t arrays make use of DSP instructions.
     */
    void operator+=(const Array& another)
    { 
      addArrayToArray((T*)another, _array, _array, _shape.columns);
    }
    void operator+=(const Array&& another)
    {
      *this+=another;
    }

    /**
     * @brief Subtract an array
     * 
     * @param another 
     * @note Float arrays make use of DSP instructions.
     */
    void operator-=(const Array& another)
    {
      subArrayFromArray((T*)another, _array, _array, _shape.columns);
    }
    void operator-=(const Array&& another)
    {
      *this-=another;
    }

    /**
     * @brief Multiply by an array
     * 
     * @param another 
     * @note Float and int16_t arrays make use of DSP instructions.
     */
    void operator*=(const Array& another)
    {
      mulArrayByArray((T*)another, _array, _array, _shape.columns);
    }
    void operator*=(const Array&& another)
    {
      *this*=another;
    }

    /**
     * @brief Divide by an array
     * 
     * @param another 
     * 
     */
    void operator/=(const Array& another)
    {
      divArrayByArray(_array, another, _array, _shape.columns);
    }
    void operator/=(const Array&& another)
    {
      *this/=another;
    }

    /**
     * @brief Concatenate a value
     * 
     * @param value 
     * @return Array& 
     */
    Array& operator<<(const T value)
    {
      this->append(value);
      return *this;
    }

    /**
     * @brief Concatenate an array
     * 
     * @param another 
     * @return Array& 
     */
    Array& operator<<(const Array& another)
    {
      for(size_t i = 0; i < another.shape.columns; i++)
      {
        *this = *this << another.flatten[i];
      }
      return *this;
    }

    operator T*() const {return _array;}

    /**
     * @brief Append a value to the _array
     * 
     * @param value 
     * @return true Successful appended
     * @return false Failed to append
     */
    bool append(const T value)
    {
      assert(_shape.rows == 1);
      if(_shape.columns < _size/sizeof(T))
      {
        _array[_shape.columns] = value;
        _shape = shape2D(_shape.rows, _shape.columns+1);
        return true;
      }

      _size = _mem2alloc(_shape.columns+1);

      if (_array)
      {
        heap_caps_free(_array);
        _array = _size > 0 ? (T*)heap_caps_aligned_alloc(ALIGNMENT, _size, _caps) : NULL;
      }
      else
      {
        _array = _size > 0 ? (T*)heap_caps_aligned_alloc(ALIGNMENT, _size, _caps) : NULL;
      }

      if (_array)
      {
        _array[_shape.columns] = value;
        _shape = shape2D(_shape.rows, _shape.columns+1);
        return true;
      }
      return false;
    }

    /**
     * @brief Concatenate itself to another
     * 
     * @param another 
     * @return true Successful concatenation
     * @return false Failure during concatenation
     */
    bool append(const Array& another)
    {
      *this << another;
      return _array == NULL ? false : true;
    }

    /**
     * @brief Get the memory capabilities
     * 
     * @return const uint32_t 
     */
    uint32_t capabilities(){return _caps;}

    /**
     * @brief Verify if a value belongs to the array
     * 
     * @param value 
     * @return true 
     * @return false 
     */
    bool contain(const T value)
    {
      size_t i = 0;
      size_t len = _shape.columns*_shape.rows;
      while (i < len && _array[i] != value) i++;
      return i == len ? false : true;
    }

    /**
     * @brief Get the convolution of the array by the given kernel
     * 
     * @param kernel 
     * @return Array output array with convolution result length of (siglen + Kernel -1)
     * 
     * @note Float arrays make use of DSP instructions.
     */
    Array conv(const Array& kernel)
    {
      return *this;
    }

    /**
     * @brief Copy another array into this one
     * 
     * @param another 
     */
    void copy(const Array& another)
    {
      if (_array)
        heap_caps_free(_array);

      _shape = another.shape;
      _size = another.memSize();
      _array = _size > 0 ? (T*)heap_caps_aligned_alloc(ALIGNMENT, _size, _caps) : NULL;
      
      for(size_t i = 0; i < _shape.columns; i++)
        _array[i] = another.flatten[i];
    }

    /**
     * @brief Copy array reference and prevents array from being freed by the destructor
     * 
     * @param another 
     */
    void copyRef(Array& another)
    {
      if (_array)
        heap_caps_free(_array);

      _shape = another.shape;
      _size = another.memSize();
      _array = another.preserveMem();
    }

    /**
     * @brief Get the correlation array with the given pattern
     * 
     * @param pattern 
     * @return Array<float>
     * 
     * @note Float arrays make use of DSP instructions.
     */
    Array<float> correlation(const Array& pattern)
    {
      return *this;
    }

    /**
     * @brief Compares to another array
     * 
     * @param another Another array
     * @param EPSILON tolerance. Only used when comparing float arrays.
     * @return true Two different arrays
     * @return false Array are not different
     */
    bool diff(const Array& another, const float EPSILON = 0.0001)
    {
      size_t i = 0;
      while (i < _shape.size)
      {
        if (_array[i]!=another.flatten[i])
          return true;
        i++;
      }
      return false;
    }

    /**
     * @brief Get the allocated memory bytes
     * 
     * @return size_t 
     */
    size_t memSize() const
    {
      return _size;
    }

    /**
     * @brief 
     * 
     * @param newFrac New Fractional bits value.
     * @note It only accepts new values for int16_t arrays.
     */
    void updateFractional(uint8_t newFrac)
    {
      if (sizeof(T) == 2) return; // It makes sure that T is 2 bytes long.
      fracBits = newFrac;
    }

    const shape2D& shape = _shape;
    T* const& flatten = _array;
    const uint8_t& frac = fracBits;
  protected:
    T* _array = NULL;/*Array pointer*/
    size_t _size = 0; /*Total bytes allocated*/
    shape2D _shape = shape2D(1,0);
    uint32_t _caps = this->memCaps();
    uint8_t fracBits = 0; /* Fractional bits of a fixed point array*/

  private:
    bool canBeDestroyed = true;

    /**
     * @brief Get the total bytes to be 16 bytes aligned allocated
     * 
     * @param blocks The quantity of memory blocks
     * @return size_t 
     */
    size_t _mem2alloc(const size_t blocks)
    {
      return blocks*sizeof(T);
    }

    /**
     * @brief Return the allocated memory capabilities.
     * 
     * @return uint32_t
     */
    static uint32_t memCaps()
    {
      return MALLOC_CAP_8BIT;
    }

    /**
     * @brief Prevents array from being freed by the destructor
     * 
     * @return T* 
     */
    T* preserveMem()
    {
      canBeDestroyed = false;
      return _array;
    }
  };

  template<>
  inline uint32_t Array<int32_t>::memCaps(){return MALLOC_CAP_32BIT;}
  template<>
  inline uint32_t Array<uint32_t>::memCaps(){return MALLOC_CAP_32BIT;}

  template<>
  inline bool Array<int32_t>::isDSPSupported(){return true;}
  template<>
  inline bool Array<int16_t>::isDSPSupported(){return true;}
  template<>
  inline bool Array<int8_t>::isDSPSupported(){return true;}
  template<>
  inline bool Array<float>::isDSPSupported(){return true;}

  // template<>
  // inline Array<int32_t>::operator int32_t*() const {return _array;}
  // template<>
  // inline Array<int16_t>::operator int16_t*() const {return _array;}
  // template<>
  // inline Array<int8_t>::operator int8_t*() const {return _array;}
  // template<>
  // inline Array<float>::operator float*() const {return _array;}

  template<>
  inline bool Array<float>::diff(const Array<float>& another, const float EPSILON)
  {
    size_t i = 0;
    while(i < _shape.columns)
    {
      if (!eqFloats(_array[i], another.flatten[i], EPSILON))
        return true;
      i++;
    }
    return false;
  }

#ifdef CONFIG_IDF_TARGET_ESP32S3
#if CONFIG_IDF_TARGET_ESP32S3

  template<>
  inline void Array<float>::operator+=(const float value)
  {
    exec_dsp(dsps_addc_f32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<int32_t>::operator+=(const int32_t value)
  {
    exec_dsp(dsps_addc_s32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<uint32_t>::operator+=(const uint32_t value)
  {
    exec_dsp(dsps_addc_s32_esp, (int32_t*)_array, (int32_t*)_array, _shape.columns, value);
  }

  template<>
  inline void Array<int16_t>::operator+=(const int16_t value)
  {
    exec_dsp(dsps_addc_s16_esp, _array, _array, _shape.columns, &value, 1, 1, 0);
  }

  template<>
  inline void Array<int8_t>::operator+=(const int8_t value)
  {
    exec_dsp(dsps_addc_s8_esp, _array, _array, _shape.columns, &value);
  }

  template<>
  inline void Array<float>::operator-=(const float value)
  {
    exec_dsp(dsps_subc_f32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<int32_t>::operator-=(const int32_t value)
  {
    exec_dsp(dsps_subc_s32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<uint32_t>::operator-=(const uint32_t value)
  {
    exec_dsp(dsps_subc_s32_esp, (int32_t*)_array, (int32_t*)_array, _shape.columns, value);
  }

  template<>
  inline void Array<int16_t>::operator-=(const int16_t value)
  {
    exec_dsp(dsps_subc_s16_esp, _array, _array, _shape.columns, &value, 1, 1, 0);
  }

  template<>
  inline void Array<int8_t>::operator-=(const int8_t value)
  {
    exec_dsp(dsps_subc_s8_esp, _array, _array, _shape.columns, &value);
  }

  template<>
  inline void Array<float>::operator*=(const float value)
  {
    exec_dsp(dsps_mulc_f32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<int32_t>::operator*=(const int32_t value)
  {
    exec_dsp(dsps_mulc_s32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<uint32_t>::operator*=(const uint32_t value)
  {
    exec_dsp(dsps_mulc_s32_esp, (int32_t*)_array, (int32_t*)_array, _shape.columns, (int32_t)value);
  }

  template<>
  inline void Array<int8_t>::operator*=(const int8_t value)
  {
    exec_dsp(dsps_mulc_s8_esp, _array, _array, _shape.columns, &value);
  }

  template<>
  inline void Array<int16_t>::operator*=(const int16_t value)
  {
    exec_dsp(dsps_mulc_s16_esp, _array, _array, _shape.columns, value, 1, 1, Array<int16_t>::frac);
  }

  template<>
  inline void Array<float>::operator/=(const float value)
  {
    exec_dsp(dsps_divc_f32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<int32_t>::operator/=(const int32_t value)
  {
    exec_dsp(dsps_divc_s32_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<uint32_t>::operator/=(const uint32_t value)
  {
    exec_dsp(dsps_divc_s32_esp, (int32_t*)_array, (int32_t*)_array, _shape.columns, (int32_t)value);
  }

  template<>
  inline void Array<int16_t>::operator/=(const int16_t value)
  {
    exec_dsp(dsps_divc_s16_esp, _array, _array, _shape.columns, value, 1, 1, Array<int16_t>::frac);
  }

  template<>
  inline void Array<int8_t>::operator/=(const int8_t value)
  {
    exec_dsp(dsps_divc_s8_esp, _array, _array, _shape.columns, value);
  }

  template<>
  inline void Array<float>::operator+=(const Array<float>& another)
  {
    exec_dsp(dsps_add_f32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<int32_t>::operator+=(const Array<int32_t>& another)
  {
    exec_dsp(dsps_add_s32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<uint32_t>::operator+=(const Array<uint32_t>& another)
  {
    exec_dsp(dsps_add_s32_esp, (int32_t*)_array, (int32_t*)another.flatten, (int32_t*)_array, _shape.columns);
  }

  template<>
  inline void Array<int16_t>::operator+=(const Array<int16_t>& another)
  {
    exec_dsp(dsps_add_s16_esp, _array, another, _array, _shape.columns, 1, 1, 1, 0);
  }

  template<>
  inline void Array<int8_t>::operator+=(const Array<int8_t>& another)
  {
    exec_dsp(dsps_add_s8_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<float>::operator-=(const Array<float>& another)
  {
    exec_dsp(dsps_sub_f32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<int32_t>::operator-=(const Array<int32_t>& another)
  {
    exec_dsp(dsps_sub_s32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<uint32_t>::operator-=(const Array<uint32_t>& another)
  {
    exec_dsp(dsps_sub_s32_esp, (int32_t*)_array, (int32_t*)another.flatten, (int32_t*)_array, _shape.columns);
  }

  template<>
  inline void Array<int16_t>::operator-=(const Array<int16_t>& another)
  {
    exec_dsp(dsps_sub_s16_esp, _array, another, _array, _shape.columns, 1, 1, 1, 0);
  }

  template<>
  inline void Array<int8_t>::operator-=(const Array<int8_t>& another)
  {
    exec_dsp(dsps_sub_s8_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<float>::operator*=(const Array<float>& another)
  {
    exec_dsp(dsps_mul_f32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<int32_t>::operator*=(const Array<int32_t>& another)
  {
    exec_dsp(dsps_mul_s32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<uint32_t>::operator*=(const Array<uint32_t>& another)
  {
    exec_dsp(dsps_mul_s32_esp, (int32_t*)_array, (int32_t*)another.flatten, (int32_t*)_array, _shape.columns);
  }

  template<>
  inline void Array<int16_t>::operator*=(const Array<int16_t>& another)
  {
    exec_dsp(dsps_mul_s16_esp,_array, another, _array, _shape.columns, 1, 1, 1, Array<int16_t>::frac);
  }

  template<>
  inline void Array<int8_t>::operator*=(const Array<int8_t>& another)
  {
    exec_dsp(dsps_mul_s8_esp,_array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<float>::operator/=(const Array<float>& another)
  {
    exec_dsp(dsps_div_f32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<int32_t>::operator/=(const Array<int32_t>& another)
  {
    exec_dsp(dsps_div_s32_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline void Array<int16_t>::operator/=(const Array<int16_t>& another)
  {
    exec_dsp(dsps_div_s16_esp, _array, another, _array, _shape.columns, 1, 1, 1, Array<int16_t>::frac);
  }

  template<>
  inline void Array<int8_t>::operator/=(const Array<int8_t>& another)
  {
    exec_dsp(dsps_div_s8_esp, _array, another, _array, _shape.columns);
  }

  template<>
  inline Array<float> Array<float>::operator==(const float value)
  {
    Array<float> newArray(_shape);
    for (size_t i = 0; i < _shape.size; i++)
      newArray.flatten[i] = (float)eqFloats(_array[i], value);
    return newArray;
  }

  template<>
  inline bool Array<float>::operator==(const float* input)
  {
    size_t i = 0;
    while( i < _shape.size)
    {
      if (!eqFloats(_array[i], input[i]))
        return false;
      i++;
    }
    return true;
  }

  template<>
  inline Array<float> Array<float>::conv(const Array<float>& kernel);

  template<>
  inline Array<float> Array<float>::correlation(const Array<float>& pattern);

  Array<float> operator+(const Array<float>& onearray, const Array<float> another);
  Array<int32_t> operator+(const Array<int32_t>& onearray, const Array<int32_t> another);
  Array<uint32_t> operator+(const Array<uint32_t>& onearray, const Array<uint32_t> another);
  Array<int16_t> operator+(const Array<int16_t>& onearray, const Array<int16_t> another);
  Array<int8_t> operator+(const Array<int8_t>& onearray, const Array<int8_t> another);
  Array<float> operator+(const Array<float>& onearray, const float value);
  Array<int32_t> operator+(const Array<int32_t>& onearray, const int32_t value);
  Array<uint32_t> operator+(const Array<uint32_t>& onearray, const uint32_t value);
  Array<int16_t> operator+(const Array<int16_t>& onearray, const int16_t value);
  Array<int8_t> operator+(const Array<int8_t>& onearray, const int8_t value);
  Array<float> operator+(const float value, const Array<float> onearray);
  Array<int32_t> operator+(const int32_t value, const Array<int32_t> onearray);
  Array<uint32_t> operator+(const uint32_t value, const Array<uint32_t> onearray);
  Array<int16_t> operator+(const int16_t value, const Array<int16_t> onearray);
  Array<int8_t> operator+(const int8_t value, const Array<int8_t> onearray);
  Array<float> operator-(const Array<float>& onearray, const Array<float> another);
  Array<int32_t> operator-(const Array<int32_t>& onearray, const Array<int32_t> another);
  Array<uint32_t> operator-(const Array<uint32_t>& onearray, const Array<uint32_t> another);
  Array<int16_t> operator-(const Array<int16_t>& onearray, const Array<int16_t> another);
  Array<int8_t> operator-(const Array<int8_t>& onearray, const Array<int8_t> another);
  Array<float> operator-(const Array<float>& onearray, const float value);
  Array<int32_t> operator-(const Array<int32_t>& onearray, const int32_t value);
  Array<uint32_t> operator-(const Array<uint32_t>& onearray, const uint32_t value);
  Array<int16_t> operator-(const Array<int16_t>& onearray, const int16_t value);
  Array<int8_t> operator-(const Array<int8_t>& onearray, const int8_t value);
  Array<float> operator-(const float value, const Array<float> onearray);
  Array<int32_t> operator-(const int32_t value, const Array<int32_t> onearray);
  Array<uint32_t> operator-(const uint32_t value, const Array<uint32_t> onearray);
  Array<int16_t> operator-(const int16_t value, const Array<int16_t> onearray);
  Array<int8_t> operator-(const int8_t value, const Array<int8_t> onearray);
  Array<float> operator*(const Array<float>& onearray, const Array<float> another);
  Array<int32_t> operator*(const Array<int32_t>& onearray, const Array<int32_t> another);
  Array<uint32_t> operator*(const Array<uint32_t>& onearray, const Array<uint32_t> another);
  Array<int16_t> operator*(const Array<int16_t>& onearray, const Array<int16_t> another);
  Array<int8_t> operator*(const Array<int8_t>& onearray, const Array<int8_t> another);
  Array<float> operator*(const Array<float>& onearray, const float value);
  Array<int32_t> operator*(const Array<int32_t>& onearray, const int32_t value);
  Array<uint32_t> operator*(const Array<uint32_t>& onearray, const uint32_t value);
  Array<int16_t> operator*(const Array<int16_t>& onearray, const int16_t value);
  Array<int8_t> operator*(const Array<int8_t>& onearray, const int8_t value);
  Array<float> operator*(const float value, const Array<float> onearray);
  Array<int32_t> operator*(const int32_t value, const Array<int32_t> onearray);
  Array<uint32_t> operator*(const uint32_t value, const Array<uint32_t> onearray);
  Array<int16_t> operator*(const int16_t value, const Array<int16_t> onearray);
  Array<int8_t> operator*(const int8_t value, const Array<int8_t> onearray);
  Array<float> operator/(const Array<float>& onearray, const float value);
  Array<int32_t> operator/(const Array<int32_t>& onearray, const int32_t value);
  Array<int16_t> operator/(const Array<int16_t>& onearray, const int16_t value);
  Array<int8_t> operator/(const Array<int8_t>& onearray, const int8_t value);
  Array<float> operator/(const float value, const Array<float> another);
  Array<int32_t> operator/(const int32_t value, const Array<int32_t> onearray);
  Array<int16_t> operator/(const int16_t value, const Array<int16_t> onearray);
  Array<int8_t> operator/(const int8_t value, const Array<int8_t> onearray);
  Array<float> operator/(const Array<float>& onearray, const Array<float> another);
  Array<int32_t> operator/(const Array<int32_t>& onearray, const Array<int32_t> another);
  Array<int16_t> operator/(const Array<int16_t>& onearray, const Array<int16_t> another);
  Array<int8_t> operator/(const Array<int8_t>& onearray, const Array<int8_t> another);
  float operator^(const Array<float>& onearray, const Array<float> another);
  int16_t operator^(const Array<int16_t>& onearray, const Array<int16_t> another);
  int32_t operator^(const Array<int32_t>& onearray, const Array<int32_t> another);
  int8_t operator^(const Array<int8_t>& onearray, const Array<int8_t> another);

#endif
#endif
}

#endif