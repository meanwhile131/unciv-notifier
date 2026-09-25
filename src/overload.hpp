namespace detail {
	template <class... Fs>
		struct overload;

	template <class F>
		struct overload<F> : public F {
			explicit overload(F func) : F(func) {
			}
		};
	template <class F, class... Fs>
		struct overload<F, Fs...>
		: public overload<F>
		, public overload<Fs...> {
			explicit overload(F func, Fs... funcs) : overload<F>(func), overload<Fs...>(funcs...) {
			}
			using overload<F>::operator();
			using overload<Fs...>::operator();
		};
}  // namespace detail

template <class... F>
auto overloaded(F... func) {
	return detail::overload<F...>(func...);
}
